// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 StackBlender LLC
//
// Saving and loading the DSP-1's state anywhere in a stream of commands changes nothing:
// at every byte of the stream, the chip's state is saved, loaded into a fresh chip, and
// both are run to the end; every byte read afterwards must match the unbroken run.

#include "dsp1/Dsp1.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct Op { bool write; uint8_t value; };

void words(std::vector<Op>& s, std::initializer_list<int> ws) {
	for(int w : ws) {
		s.push_back({true, uint8_t(w)});
		s.push_back({true, uint8_t(uint16_t(w) >> 8)});
	}
}
void reads(std::vector<Op>& s, int bytes) {
	for(int i = 0; i < bytes; i++) s.push_back({false, 0});
}

std::vector<Op> stream() {
	std::vector<Op> s;
	s.push_back({true, 0x01}); words(s, {0x4000, 0x1234, -0x2000, 0x0800});        // Attitude A
	s.push_back({true, 0x0d}); words(s, {0x0100, -0x0200, 0x0300}); reads(s, 6);    // Objective A
	s.push_back({true, 0x02}); words(s, {0x0400, 0x0300, 0x0080, 0x0040, 0x0100, 0x2000, 0x0c00});
	reads(s, 8);                                                                     // Parameter
	for(int i = 0; i < 3; i++) {
		s.push_back({true, 0x06}); words(s, {0x0120 + i * 40, 0x0210 - i * 30, -0x0020}); reads(s, 6);
	}                                                                                // Project
	s.push_back({true, 0x0a}); words(s, {0x0010}); reads(s, 24); words(s, {0});      // Raster
	s.push_back({true, 0x0e}); words(s, {0x0040, 0x0050}); reads(s, 6);             // Target
	s.push_back({true, 0x08}); words(s, {0x1000, 0x2000, 0x3000}); reads(s, 4);     // Radius
	s.push_back({true, 0x80});                                                       // resync
	s.push_back({true, 0x28}); words(s, {0x1000, 0x2000, 0x0800}); reads(s, 2);     // Distance
	return s;
}

std::vector<uint8_t> run(dsp1::Dsp1& chip, const std::vector<Op>& s, size_t from) {
	std::vector<uint8_t> out;
	for(size_t i = from; i < s.size(); i++) {
		if(s[i].write) chip.writeData(s[i].value);
		else out.push_back(chip.readData());
		out.push_back(chip.readStatus());
	}
	return out;
}

} // namespace

int main() {
	int failures = 0;
	for(auto revision : {dsp1::Dsp1::Revision::Dsp1, dsp1::Dsp1::Revision::Dsp1B}) {
		auto s = stream();
		for(size_t cut = 0; cut <= s.size(); cut++) {
			dsp1::Dsp1 a(revision);
			run(a, std::vector<Op>(s.begin(), s.begin() + cut), 0);
			auto blob = a.saveState();
			dsp1::Dsp1 b(dsp1::Dsp1::Revision::Dsp1B == revision ? dsp1::Dsp1::Revision::Dsp1 : dsp1::Dsp1::Revision::Dsp1B);
			if(!b.loadState(blob.data(), blob.size())) {
				std::printf("cut %zu: load refused\n", cut);
				failures++;
				continue;
			}
			if(b.saveState() != blob) {
				std::printf("cut %zu: state doesn't round-trip\n", cut);
				failures++;
			}
			if(run(a, s, cut) != run(b, s, cut)) {
				std::printf("cut %zu: answers differ after loading\n", cut);
				failures++;
			}
		}
		// Damaged or foreign blobs are refused and change nothing.
		dsp1::Dsp1 c(revision);
		auto blob = c.saveState();
		auto before = blob;
		for(size_t n = 0; n < blob.size(); n++)
			if(c.loadState(blob.data(), n)) { std::printf("truncated blob (%zu bytes) accepted\n", n); failures++; }
		blob[0] ^= 1;
		if(c.loadState(blob.data(), blob.size())) { std::printf("bad magic accepted\n"); failures++; }
		if(c.saveState() != before) { std::printf("a refused load changed the chip\n"); failures++; }
	}
	std::printf("%s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
