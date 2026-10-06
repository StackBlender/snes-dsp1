// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 StackBlender
#include "dsp1/Dsp1.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dsp1 {

namespace {

// Commands: parameters in, results out (-1: Raster's stream). Opcodes not listed take
// none and return none.
struct Shape {
	int in, out;
};
Shape shapeOf(uint8_t op) {
	switch(op & 0x3f) {
	case 0x00: case 0x20: return {2, 1};           // Multiply
	case 0x10: case 0x30: return {2, 2};           // Inverse
	case 0x04: case 0x24: return {2, 2};           // Triangle
	case 0x14: case 0x34: return {6, 3};           // Gyrate
	case 0x08: case 0x28: return op & 0x20 ? Shape{3, 1} : Shape{3, 2}; // Radius / Distance
	case 0x18: case 0x38: return {4, 1};           // Range
	case 0x0c: case 0x2c: return {3, 2};           // Rotate
	case 0x1c: case 0x3c: return {6, 3};           // Polar
	case 0x02: case 0x12: case 0x22: case 0x32: return {7, 4}; // Parameter
	case 0x0a: case 0x1a: case 0x2a: case 0x3a: return {1, -1}; // Raster
	case 0x06: case 0x16: case 0x26: case 0x36: return {3, 3}; // Project
	case 0x0e: case 0x1e: case 0x2e: case 0x3e: return {2, 2}; // Target
	case 0x01: case 0x05: case 0x31: case 0x35:
	case 0x11: case 0x15: case 0x21: case 0x25: return {4, 0}; // Attitude A/B/C
	case 0x0d: case 0x09: case 0x39: case 0x3d:
	case 0x1d: case 0x19: case 0x2d: case 0x29: return {3, 3}; // Objective A/B/C
	case 0x03: case 0x33: case 0x13: case 0x23: return {3, 3}; // Subjective A/B/C
	case 0x0b: case 0x3b: case 0x1b: case 0x2b: return {3, 1}; // Scalar A/B/C
	case 0x0f: case 0x1f: case 0x2f: case 0x3f: return {1, 1}; // memory test/size
	}
	return {0, 0};
}

int matrixOf(uint8_t op) {
	switch(op & 0x3f) {
	case 0x11: case 0x15: case 0x1d: case 0x19: case 0x13: case 0x1b: return 1;
	case 0x21: case 0x25: case 0x2d: case 0x29: case 0x23: case 0x2b: return 2;
	}
	return 0;
}

// 1.15 multiply, truncated (the chip's: exact on every probed input).
inline int32_t mul(int32_t a, int32_t b) { return (a * b) >> 15; }
inline int16_t wrap(int32_t v) { return int16_t(uint16_t(v)); }
inline int32_t clamp15(int64_t v) { return int32_t(std::clamp<int64_t>(v, -32767, 32767)); }
inline int32_t clamp16(int64_t v) { return int32_t(std::clamp<int64_t>(v, -32768, 32767)); }
// v * 2^n, arithmetic.
inline int64_t shiftBy(int64_t v, int n) { return n >= 0 ? v << n : v >> -n; }

// Shifts v left until it fills 16 bits (bits 15 and 14 differ), counting the shifts.
int32_t normalise(int32_t v, int& shifts) {
	shifts = 0;
	if(v == 0) return 0;
	while(v >= -0x4000 && v < 0x4000) {
		v <<= 1;
		shifts++;
	}
	return v;
}

// The chip's saturation in its scale results: past either end it gives +/-32767, but
// exactly -32768 passes.
int32_t saturate(int64_t v) {
	if(v < -32768) return -32767;
	return int32_t(std::min<int64_t>(v, 32767));
}

// A wider value as a 16-bit mantissa filling its bits and a shift: v ~ m * 2^shift (the
// bits below the mantissa dropped, rounding down).
int32_t normaliseWide(int64_t v, int& shift) {
	shift = 0;
	if(v == 0) return 0;
	auto at = [&](int k) { return k >= 0 ? v >> k : v * (int64_t(1) << -k); };
	while(at(shift) < -0x8000 || at(shift) >= 0x8000) shift++;
	while(at(shift) >= -0x4000 && at(shift) < 0x4000) shift--;
	return int32_t(at(shift));
}

// The Inverse command's arithmetic, which the camera commands use too: 1 / (m * 2^e) as a
// mantissa and exponent (m and the result as 1.15 fixed point). Exact on every input.
struct Inverse {
	int32_t m;
	int e;
};
Inverse inverse(int32_t m, int32_t e) {
	if(m == 0) return {0x7fff, 0x2f};
	bool negative = m < 0;
	if(negative) m = m == -32768 ? 32768 : -m;
	while(m < 0x4000) {
		m <<= 1;
		e--;
	}
	if(m >= 0x8000) {
		m >>= 1;
		e++;
	}
	// For a power of two the chip gives 32767, or -16384 and one more in the exponent when
	// negative.
	if(m == 0x4000) return {negative ? -16384 : 32767, int(1 - e + negative)};
	// A first guess for each of 128 ranges of m, round(2^29 / m0) at the range's start (a
	// formula, not the chip's data), then two Newton steps r = r (2 - m r), each in the
	// chip's format (r is 2^29 / m, the 1.15 product truncated, the result kept even).
	int32_t m0 = m & ~127;
	int32_t r = std::min<int32_t>(32767, ((1 << 29) + m0 / 2) / m0);
	for(int i = 0; i < 2; i++) r = ((r * (32768 - ((m * r) >> 15))) >> 15) << 1;
	return {negative ? -r : r, int(1 - e)};
}

// Square root, as the chip computes it (exact on every probed sum below 2^30): the sum is
// shifted left two bits at a time until bit 28 or 29 is its top bit; the result is
// interpolated from 48 points of 32767 sqrt(m / 32768), m from 8192 to 32768 in steps of
// 512 (a formula, not the chip's data), then shifted back. For sums from 2^25 to 2^26 the
// chip keeps fewer bits: it shifts the sum's top 16 bits, not the whole sum.
// From 2^30 up (e.g. two components past 23170; no game seen sends one) the chip's
// result is unlike a square root (it runs past its table); ours is the true root, capped.
// The first revision's table gives each odd segment's start one segment low: there the
// value is T[j] - (T[j+1] - T[j]) plus the usual slope (black-box: every probe matches).
int16_t distance(int64_t r2, bool firstRevision) {
	static const auto table = [] {
		std::array<int32_t, 49> t{};
		for(int j = 0; j <= 48; j++) t[j] = int32_t(std::floor(32767 * std::sqrt((8192 + 512 * j) / 32768.0) + 1e-4));
		return t;
	}();
	if(r2 >= (int64_t(1) << 30)) return int16_t(std::min<int64_t>(32767, int64_t(std::sqrt(double(r2)))));
	if(r2 == 0) return 0;
	int n = 0;
	int32_t m;
	if(r2 >> 25 == 1) {
		m = int32_t(r2 >> 15) << 4;
		n = 2;
	} else {
		while(r2 < (int64_t(1) << 28)) r2 <<= 2, n++;
		m = int32_t(r2 >> 15);
	}
	int j = (m - 8192) >> 9, f = (m - 8192) & 511;
	int32_t slope = table[j + 1] - table[j];
	int32_t start = table[j] - (firstRevision && (j & 1) ? slope : 0);
	return int16_t((start + ((slope * f) >> 9)) >> n);
}

// Sine and cosine at 256 table points: trunc(32768 sin(2 pi k / 256)), capped (a formula,
// not the chip's data: it reproduces every value the chip gives).
struct Tables {
	std::array<int16_t, 256> s, c;
	Tables() {
		for(int k = 0; k < 256; k++) {
			double x = 2 * M_PI * k / 256;
			s[k] = int16_t(std::clamp<long>(long(32768 * std::sin(x)), -32768, 32767));
			c[k] = int16_t(std::clamp<long>(long(32768 * std::cos(x)), -32768, 32767));
		}
	}
};
const Tables& tables() {
	static const Tables t;
	return t;
}

// Between table points, one tangent step: the point's value plus the slope times the
// offset, the offset taken as floor(f * 3217 / 1024) (pi per angle unit).
int32_t sinPositive(int32_t a) { // 0 <= a <= 32768
	auto& t = tables();
	int k = (a >> 8) & 255, f = a & 0xff;
	int32_t v = t.s[k] + mul(t.c[k], (f * 3217) >> 10);
	return std::min(v, 32767);
}
int32_t cosPositive(int32_t a) {
	auto& t = tables();
	int k = (a >> 8) & 255, f = a & 0xff;
	int32_t v = t.c[k] - mul(t.s[k], (f * 3217) >> 10);
	if(v < -32768) return -32767; // the chip's, past -32768
	return std::min(v, 32767);
}

} // namespace

int16_t Dsp1::sin(int16_t angle) {
	int32_t a = angle;
	return int16_t(a >= 0 ? sinPositive(a) : -sinPositive(-a));
}

int16_t Dsp1::cos(int16_t angle) {
	int32_t a = angle;
	return int16_t(cosPositive(a == -32768 ? 32768 : std::abs(a)));
}

void Dsp1::reset() {
	m_mode = Mode::Command;
	m_in.clear();
	m_out.clear();
	m_outAt = 0;
	m_highByte = false;
	m_raster = false;
}

uint8_t Dsp1::readStatus() const {
	uint8_t s = 0x80; // always ready: the work is done instantly
	if(m_mode == Mode::Command) s |= 0x04;
	else if(m_highByte) s |= 0x10;
	return s;
}

void Dsp1::writeData(uint8_t value) {
	if(m_mode == Mode::Command) {
		if(value & 0x80) return; // resynchronising
		begin(value);
		return;
	}
	if(!m_highByte) {
		m_lowByte = value;
		m_highByte = true;
		return;
	}
	m_highByte = false;
	int16_t word = int16_t(m_lowByte | value << 8);
	if(m_mode == Mode::Input) {
		m_in.push_back(word);
		if(int(m_in.size()) == m_need) execute();
		return;
	}
	// A word written while results are due takes one result's place; Raster stops at the
	// end of the line it was in.
	if(m_raster) {
		m_outAt++;
		if(++m_rasterWrites >= 4 && m_outAt >= m_out.size()) {
			m_raster = false;
			m_mode = Mode::Command;
		}
		return;
	}
	if(++m_outAt >= m_out.size()) m_mode = Mode::Command;
}

uint8_t Dsp1::readData() {
	if(m_mode != Mode::Output) return m_lastRead;
	if(m_outAt >= m_out.size()) {
		if(!m_raster) {
			m_mode = Mode::Command;
			return m_lastRead;
		}
		nextRasterLine();
	}
	uint16_t w = uint16_t(m_out[m_outAt]);
	if(!m_highByte) {
		m_highByte = true;
		return m_lastRead = uint8_t(w);
	}
	m_highByte = false;
	m_outAt++;
	if(m_outAt >= m_out.size()) {
		if(m_raster) m_work = kRasterLine;
		else m_mode = Mode::Command;
	}
	return m_lastRead = uint8_t(w >> 8);
}

void Dsp1::begin(uint8_t op) {
	m_op = op;
	Shape s = shapeOf(op);
	m_need = s.in;
	m_in.clear();
	m_out.clear();
	m_outAt = 0;
	m_highByte = false;
	m_raster = false;
	if(m_need == 0) {
		execute();
		return;
	}
	m_mode = Mode::Input;
}

// Parameter: the camera. In: the base point Fx, Fy, Fz, the eye's distance behind it Lfe,
// the screen's distance from the eye Les, the azimuth Aas and the zenith angle Azs. Out:
// Vof and Vva (screen lines: the shift of the screen's centre when the zenith angle is past
// its limit, and the horizon's line), and Cx, Cy (the ground point at the screen's centre).
// Exact on every probed camera up to the limit (Azs about 80 degrees, given the inverse);
// past it, not yet worked out (an approximation).
void Dsp1::parameter() {
	auto& c = m_camera;
	auto& in = m_in;
	c = {in[0], in[1], in[2], in[3], in[4], in[5], in[6]};
	const int32_t kLimit = 14532; // the zenith angle's limit, about 79.8 degrees (approximate: the chip's varies a little with the height)
	int32_t azs = c.azs, vof = 0;
	bool past = azs > kLimit || azs < -kLimit;
	if(past) azs = azs > 0 ? kLimit : -kLimit;
	c.sinZ = sin(int16_t(azs));
	c.cosZ = cos(int16_t(azs));
	c.sinA = sin(c.aas);
	c.cosA = cos(c.aas);
	c.screen = (c.les * c.cosZ) >> 15;

	// The horizon: -Les cos / sin, as (Les cos) times 1 / sin.
	int32_t vva = 0;
	if(int32_t t = c.screen) {
		int s;
		int32_t tn = normalise(t, s);
		Inverse r = inverse(c.sinZ, 0);
		vva = clamp15(shiftBy(-((tn * r.m) >> 15), r.e - s));
	}
	if(past) { // approximate: the screen keeps the limit's angle and moves by Les tan(over)
		int32_t over = c.azs - azs;
		Inverse r = inverse(cos(int16_t(over)), 0);
		vof = clamp15(shiftBy(int64_t(c.les) * ((sin(int16_t(over)) * r.m) >> 15), r.e - 15));
	}

	// The eye: Lfe behind the base point along the view; the ground point at the centre
	// is T along the view from it, T the eye's height over cos Azs.
	c.height = wrap(c.fz + ((c.lfe * ((c.cosZ * 32767) >> 15)) >> 15));
	int32_t t = 0;
	if(c.height) {
		int s;
		int32_t hn = normalise(c.height, s);
		Inverse r = inverse(c.cosZ, 0);
		t = clamp16(shiftBy((hn * r.m) >> 15, r.e - s));
	}
	int32_t along = (t * c.sinZ) >> 15;
	int32_t ux = (-c.sinZ * c.sinA) >> 15, uy = (c.sinZ * c.cosA) >> 15;
	push(int16_t(vof));
	push(int16_t(vva));
	push(wrap(c.fx + ((c.lfe * ux) >> 15) + ((along * c.sinA) >> 15)));
	push(wrap(c.fy + ((c.lfe * uy) >> 15) - ((along * c.cosA) >> 15)));
}

// The ground's distance per pixel on screen line v (times 256): the eye's height over
// d = Les cos Azs + v sin Azs across the view (the result), and over d cos Azs along it (y).
int32_t Dsp1::rasterScale(int32_t line, int32_t& y, int half) const {
	auto& c = m_camera;
	y = 0;
	if(!c.height) return 0;
	int32_t d = wrap(c.screen + ((line * c.sinZ) >> 15));
	Inverse r = inverse(d, 0), rc = inverse(c.cosZ, 0);
	int s;
	int32_t hn = normalise(c.height, s);
	int32_t xm = (hn * r.m) >> 15;
	y = saturate(shiftBy((xm * rc.m) >> 15, rc.e + r.e - s - 7 - half));
	return saturate(shiftBy(xm, r.e - s - 7 - half));
}

// Raster: Mode 7's A, B, C, D for each screen line from the one given: the line's scales
// turned by the azimuth.
void Dsp1::nextRasterLine() {
	m_out.clear();
	m_outAt = 0;
	auto& c = m_camera;
	int32_t y, x = rasterScale(m_rasterLine, y);
	push(int16_t((x * c.cosA) >> 15));
	push(int16_t((-y * c.sinA) >> 15));
	push(int16_t((x * c.sinA) >> 15));
	push(int16_t((y * c.cosA) >> 15));
	m_rasterLine++;
}

// Project: a point's screen position (H, V) and scale (M, 8.8) for Parameter's camera.
// The point is measured from the screen's centre (Les along the view from the eye), brought
// to one fixed point, and turned onto the camera's axes with 1.15 factors; the depth along
// the view is inverted for the scale Les / depth.
void Dsp1::project() {
	auto& c = m_camera;
	auto& in = m_in;
	int32_t ux = (-c.sinZ * c.sinA) >> 15, uy = (c.sinZ * c.cosA) >> 15;
	int32_t cz = (c.cosZ * 32767) >> 15;
	int32_t ex = c.fx + ((c.lfe * ux) >> 15), ey = c.fy + ((c.lfe * uy) >> 15);
	int32_t sx = ex - ((c.les * ux) >> 15), sy = ey - ((c.les * uy) >> 15);
	int32_t sz = c.height - ((c.les * cz) >> 15);
	int32_t d[3] = {wrap(in[0] - sx), wrap(in[1] - sy), wrap(in[2] - sz)};
	// The point from the screen centre in 1.15. The chip brings the three to a common
	// exponent with 1.15 factors: those sharing the largest's (factor 1, which 1.15 can only
	// give as 32767) come out a unit low when positive; the others are exact.
	int exps[3], top = 99;
	for(int i = 0; i < 3; i++) {
		exps[i] = 99;
		if(d[i]) normalise(d[i], exps[i]);
		top = std::min(top, exps[i]);
	}
	int64_t v[3];
	for(int i = 0; i < 3; i++) v[i] = int64_t(d[i]) * (exps[i] == top ? 32767 : 32768);
	// The products below are taken with the exact values fitting a signed 15-bit range
	// (-16384 fits, 16384 doesn't). The depth is then held with at most 6 fraction bits (a
	// further shift right when the values are small); the across and up sums keep their
	// precision until the final scaling.
	auto fits = [&](int p) {
		for(int32_t di : d) {
			int64_t w = (int64_t(di) * 32768) >> p;
			if(w < -16384 || w > 16383) return false;
		}
		return true;
	};
	int precise = 0;
	while(!fits(precise)) precise++;
	int shift = std::max(precise, 9);
	int fraction = 15 - shift, extra = shift - precise;
	int64_t x = v[0] >> precise, y = v[1] >> precise, z = v[2] >> precise;
	// The camera's axes as 1.15 factors, each product rounded down: across the view,
	// up it, and along it (Parameter's U, which also places the screen centre).
	int64_t across = ((x * ((c.cosA * 32767) >> 15)) >> 15) + ((y * ((c.sinA * 32767) >> 15)) >> 15);
	int64_t up = ((x * ((-c.sinA * c.cosZ) >> 15)) >> 15) + ((y * ((c.cosA * c.cosZ) >> 15)) >> 15) +
		((z * -c.sinZ) >> 15);
	int64_t ahead = ((x * ux) >> 15) + ((y * uy) >> 15) + ((z * cz) >> 15); // -(along the view), from the screen
	int64_t depthQ = ((int64_t(c.les) * 32768 >> precise) - ahead) >> extra;
	// Its whole part (or, when the fixed point's step is a whole unit or more, the depth
	// in those steps).
	int step = fraction > 0 ? 0 : -fraction;
	int32_t depth = fraction > 0 ? wrap(int32_t(depthQ >> fraction)) : int32_t(depthQ);
	// Measured, not yet explained: a point less than half a unit in front of the screen
	// gets the screen's depth (Les), not Les - 1.
	if(fraction > 0 && ahead > 0 && precise <= 14 && ahead <= (int64_t(1) << (14 - precise))) depth = c.les;
	Inverse r = inverse(depth, 0);
	int sl;
	int32_t k = (normalise(c.les, sl) * r.m) >> 15; // Les / depth
	auto scaled = [&](int64_t m) {
		int n = r.e - sl - fraction - step - extra;
		int64_t product = m * k;
		// Rounded down, except that a result between -1/2 and 0 comes out 0 (measured).
		if(product < 0 && n <= 14 && product > -(int64_t(1) << (14 - n))) return int16_t(0);
		return int16_t(clamp15(shiftBy(product >> 15, n)));
	};
	push(scaled(across));
	push(scaled(up));
	// The scale saturates at ±32767 when it overflows (exactly -32768 passes).
	int64_t scale = shiftBy(k, r.e - sl - 7 - step);
	push(int16_t(scale < -32768 ? -32767 : std::min<int64_t>(scale, 32767)));
}

// Target: the ground point under screen position (h, v), from Raster's scales for line v.
void Dsp1::target() {
	auto& c = m_camera;
	int32_t h = m_in[0], v = m_in[1];
	// Half the line's scales (saturating at that size), times the screen position as a
	// 16-bit word h * 256 (so only h's low byte counts), 1.15.
	int32_t y, x = rasterScale(v, y, 1);
	int32_t across = (int16_t(uint16_t(h << 8)) * x) >> 15;
	int32_t along = (int16_t(uint16_t(v << 8)) * y) >> 15;
	int32_t ux = (-c.sinZ * c.sinA) >> 15, uy = (c.sinZ * c.cosA) >> 15;
	int32_t height = c.height, t = 0;
	if(height) {
		int s;
		int32_t hn = normalise(height, s);
		Inverse r = inverse(c.cosZ, 0);
		t = clamp16(shiftBy((hn * r.m) >> 15, r.e - s));
	}
	int32_t b = (t * c.sinZ) >> 15;
	int32_t cx = c.fx + ((c.lfe * ux) >> 15) + ((b * c.sinA) >> 15);
	int32_t cy = c.fy + ((c.lfe * uy) >> 15) - ((b * c.cosA) >> 15);
	push(wrap(cx + ((across * c.cosA) >> 15) + ((-along * c.sinA) >> 15)));
	push(wrap(cy - ((across * c.sinA) >> 15) + ((along * c.cosA) >> 15)));
}

void Dsp1::execute() {
	m_work = m_op & 0x3f;
	auto& in = m_in;
	auto& matrix = m_matrix[matrixOf(m_op)];
	switch(m_op & 0x3f) {
	case 0x00: case 0x20: // Multiply
		push(int16_t(mul(in[0], in[1])));
		break;
	case 0x04: case 0x24: { // Triangle: sin and cos of an angle, times a radius
		push(int16_t(mul(sin(in[0]), in[1])));
		push(int16_t(mul(cos(in[0]), in[1])));
		break;
	}
	case 0x28: // Distance: |(x, y, z)|
		push(distance(int64_t(in[0]) * in[0] + int64_t(in[1]) * in[1] + int64_t(in[2]) * in[2],
			m_revision == Revision::Dsp1));
		break;
	case 0x08: { // Radius: 2(x^2 + y^2 + z^2), low and high words
		int64_t v = 2 * (int64_t(in[0]) * in[0] + int64_t(in[1]) * in[1] + int64_t(in[2]) * in[2]);
		push(int16_t(uint16_t(v)));
		push(int16_t(uint16_t(v >> 16)));
		break;
	}
	case 0x18: case 0x38: { // Range: (x^2 + y^2 + z^2 - r^2) in 1.15
		int64_t v = int64_t(in[0]) * in[0] + int64_t(in[1]) * in[1] + int64_t(in[2]) * in[2] - int64_t(in[3]) * in[3];
		push(wrap(int32_t(v >> 15)));
		break;
	}
	case 0x0c: case 0x2c: { // Rotate (x, y) by an angle
		int16_t c = cos(in[0]), s = sin(in[0]);
		push(wrap(mul(in[1], c) + mul(in[2], s)));
		push(wrap(mul(in[2], c) - mul(in[1], s)));
		break;
	}
	case 0x1c: case 0x3c: { // Polar: (x, y) by angle 1, then (z, x) by 2, then (y, z) by 3
		int32_t v[3] = {in[3], in[4], in[5]};
		auto rot = [](int32_t& u, int32_t& w, int16_t a) {
			int16_t c = cos(a), s = sin(a);
			int32_t nu = wrap(mul(u, c) + mul(w, s)), nw = wrap(mul(w, c) - mul(u, s));
			u = nu;
			w = nw;
		};
		rot(v[0], v[1], in[0]);
		rot(v[2], v[0], in[1]);
		rot(v[1], v[2], in[2]);
		for(int i = 0; i < 3; i++) push(int16_t(v[i]));
		break;
	}
	case 0x01: case 0x05: case 0x31: case 0x35:
	case 0x11: case 0x15: case 0x21: case 0x25: { // Attitude: scale and three angles
		int32_t h = in[0] >> 1;
		int16_t cz = cos(in[1]), sz = sin(in[1]), cy = cos(in[2]), sy = sin(in[2]), cx = cos(in[3]), sx = sin(in[3]);
		auto chain = [](int32_t start, std::initializer_list<int16_t> factors) {
			int32_t acc = start;
			for(int16_t f : factors) acc = mul(acc, f);
			return acc;
		};
		matrix[0][0] = int16_t(chain(h, {cz, cy}));
		matrix[0][1] = int16_t(-chain(h, {sz, cy}));
		matrix[0][2] = int16_t(chain(h, {sy}));
		matrix[1][0] = wrap(chain(h, {sz, cx}) + chain(h, {cz, sx, sy}));
		matrix[1][1] = wrap(chain(h, {cz, cx}) - chain(h, {sz, sx, sy}));
		matrix[1][2] = int16_t(-chain(h, {sx, cy}));
		matrix[2][0] = wrap(chain(h, {sz, sx}) - chain(h, {cz, cx, sy}));
		matrix[2][1] = wrap(chain(h, {cz, sx}) + chain(h, {sz, cx, sy}));
		matrix[2][2] = int16_t(chain(h, {cx, cy}));
		break;
	}
	case 0x0d: case 0x09: case 0x39: case 0x3d:
	case 0x1d: case 0x19: case 0x2d: case 0x29: // Objective: the matrix times a vector
		for(int i = 0; i < 3; i++)
			push(wrap(mul(matrix[i][0], in[0]) + mul(matrix[i][1], in[1]) + mul(matrix[i][2], in[2])));
		break;
	case 0x03: case 0x33: case 0x13: case 0x23: // Subjective: its transpose times a vector
		for(int i = 0; i < 3; i++)
			push(wrap(mul(matrix[0][i], in[0]) + mul(matrix[1][i], in[1]) + mul(matrix[2][i], in[2])));
		break;
	case 0x0b: case 0x3b: case 0x1b: case 0x2b: // Scalar: the matrix's first row dot a vector, one truncation
		push(wrap(int32_t((int64_t(matrix[0][0]) * in[0] + int64_t(matrix[0][1]) * in[1] + int64_t(matrix[0][2]) * in[2]) >> 15)));
		break;
	case 0x10: case 0x30: { // Inverse: 1 / (m * 2^e) as a mantissa and exponent
		Inverse r = inverse(in[0], in[1]);
		push(int16_t(r.m));
		push(int16_t(r.e));
		break;
	}
	case 0x02: case 0x12: case 0x22: case 0x32:
		parameter();
		break;
	case 0x06: case 0x16: case 0x26: case 0x36:
		project();
		break;
	case 0x14: case 0x34: { // Gyrate: attitude angles Az, Ax, Ay turned by rates U, F, L
		// Ax += U sin Ay + F cos Ay (exact); Az += (U cos Ay - F sin Ay) / cos Ax (99%
		// exact); Ay += L - (U cos Ay + F sin Ay) tan Ax (97% exact, the rest off by one).
		int32_t cy = cos(in[2]), sy = sin(in[2]), cx = cos(in[1]), sx = sin(in[1]);
		Inverse r = inverse(cx, 0);
		int k;
		int32_t dz = 0, dy = 0;
		if(int64_t w = int64_t(in[3]) * cy - int64_t(in[4]) * sy) {
			int32_t m = normaliseWide(w, k);
			dz = clamp15(shiftBy((m * r.m) >> 15, r.e + k - 15));
		}
		if(int64_t v = -(int64_t(in[3]) * cy + int64_t(in[4]) * sy); v && sx) {
			int32_t m = normaliseWide(v, k), kt;
			int32_t t = normaliseWide(int64_t(sx) * r.m, kt);
			dy = clamp15(shiftBy(-((-m * t) >> 15), r.e + k + kt - 30));
		}
		push(wrap(in[0] + dz));
		push(wrap(in[1] + ((in[3] * sy) >> 15) + ((in[4] * cy) >> 15)));
		push(wrap(in[2] + in[5] + dy));
		break;
	}
	case 0x0e: case 0x1e: case 0x2e: case 0x3e:
		target();
		break;
	case 0x0a: case 0x1a: case 0x2a: case 0x3a: // Raster
		m_raster = true;
		m_rasterLine = in[0];
		m_rasterWrites = 0;
		m_mode = Mode::Output;
		nextRasterLine();
		return;
	default: {
		Shape s = shapeOf(m_op);
		for(int i = 0; i < s.out; i++) push(0);
		break;
	}
	}
	m_outAt = 0;
	m_mode = m_out.empty() ? Mode::Command : Mode::Output;
}

} // namespace dsp1
