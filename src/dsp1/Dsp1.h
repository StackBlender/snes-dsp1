// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 StackBlender LLC
//
// A clean-room DSP-1: the maths coprocessor some SNES cartridges carry (an NEC uPD77C25
// running the console maker's program), reimplemented from public descriptions of its
// commands and black-box measurement of what the chip returns, never from its program.
//
// The SNES sees two registers: data (commands, parameters and results, a byte at a time)
// and status (bit 7: ready; bit 4: half of a 16-bit word done; bit 2: 8-bit mode, i.e.
// waiting for a command). A command is one byte; its parameters and results are 16-bit
// words, low byte first. A byte with bit 7 set, sent while a command is awaited, does
// nothing (games send $80 to resynchronise). Raster streams its results until the game
// writes a word instead of reading one.

#pragma once

#include <cstdint>
#include <vector>

namespace dsp1 {

class Dsp1 {
public:
	// The chip's two program revisions: DSP1 (the first cartridges, e.g. Pilotwings) and
	// DSP1B (the corrected one). They differ in Distance.
	enum class Revision { Dsp1, Dsp1B };
	explicit Dsp1(Revision revision = Revision::Dsp1B) : m_revision(revision) { reset(); }
	void reset();

	uint8_t readData();
	void writeData(uint8_t value);
	uint8_t readStatus() const;

	// What the chip has just started working on, reported once: a command's opcode (its
	// low 6 bits), kRasterLine for Raster's next line, or -1. For a host that models
	// how long the chip stays busy (the status register's ready bit); this class itself
	// answers at once.
	static constexpr int kRasterLine = 0x100;
	int takeWork() {
		int w = m_work;
		m_work = -1;
		return w;
	}

	// The chip's whole state, for a host's save states: a small versioned blob, the same
	// on every platform. loadState returns false (and changes nothing) if the blob isn't
	// one this class wrote.
	std::vector<uint8_t> saveState() const;
	bool loadState(const uint8_t* data, size_t size);

	// The building blocks, exposed for tests: angles are 16-bit (65536 = a full turn),
	// results 1.15 fixed point.
	static int16_t sin(int16_t angle);
	static int16_t cos(int16_t angle);

private:
	enum class Mode { Command, Input, Output };
	void begin(uint8_t op);
	void execute();
	void push(int16_t w) { m_out.push_back(w); }
	void nextRasterLine();

	Revision m_revision;
	Mode m_mode = Mode::Command;
	uint8_t m_op = 0;
	int m_need = 0;
	std::vector<int16_t> m_in, m_out;
	size_t m_outAt = 0;
	bool m_highByte = false; // the next byte is a word's high byte
	uint8_t m_lowByte = 0;
	uint8_t m_lastRead = 0;
	bool m_raster = false;
	int m_work = -1;
	int16_t m_rasterLine = 0;
	int m_rasterWrites = 0;

	// Attitude matrices A, B and C (Attitude sets one; Objective, Subjective and Scalar use it).
	int16_t m_matrix[3][3][3] = {};
	// Parameter's camera, for Project, Raster and Target.
	struct Camera {
		int16_t fx = 0, fy = 0, fz = 0, lfe = 0, les = 0, aas = 0, azs = 0;
		int32_t sinZ = 0, cosZ = 0, sinA = 0, cosA = 0; // of the zenith and azimuth angles
		int32_t height = 0; // the eye's height (Fz + Lfe cos Azs)
		int32_t screen = 0; // Les cos Azs
	} m_camera;
	void parameter();
	void project();
	void target();
	int32_t rasterScale(int32_t line, int32_t& y, int half = 0) const;
	int heightShifts() const;
	int32_t zenithLimit() const;
	int32_t limitCosine() const;
	int32_t effectiveCosine(int32_t over) const;
};

} // namespace dsp1
