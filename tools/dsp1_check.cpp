// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 StackBlender
//
// dsp1_check ANSWERS...: replays recorded questions and answers (lines "<opcode> <inputs...>
// = <outputs...>", hex words) through the DSP-1, byte by byte through its registers, and
// reports per command how many answers it gives exactly. A line "w <words...> =" writes
// words with no command (ending Raster's stream). Exits nonzero if any differs (with
// --strict). --dsp1: the files after it are from the first revision (default DSP1B).

#include "dsp1/Dsp1.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
	bool strict = false;
	auto revision = dsp1::Dsp1::Revision::Dsp1B;
	std::map<int, std::pair<long, long>> stats; // opcode -> (exact, total)
	std::map<int, std::string> firstMiss;
	for(int i = 1; i < argc; i++) {
		if(!std::strcmp(argv[i], "--strict")) {
			strict = true;
			continue;
		}
		if(!std::strcmp(argv[i], "--dsp1")) {
			revision = dsp1::Dsp1::Revision::Dsp1;
			continue;
		}
		std::ifstream f(argv[i]);
		std::string line;
		dsp1::Dsp1 chip(revision);
		while(std::getline(f, line)) {
			auto eq = line.find('=');
			if(eq == std::string::npos) continue;
			std::stringstream left(line.substr(0, eq)), right(line.substr(eq + 1));
			unsigned op, v;
			if(left.peek() == 'w') {
				left.get();
				while(left >> std::hex >> v) {
					chip.writeData(uint8_t(v));
					chip.writeData(uint8_t(v >> 8));
				}
				continue;
			}
			left >> std::hex >> op;
			std::vector<unsigned> in, want, got;
			while(left >> std::hex >> v) in.push_back(v);
			while(right >> std::hex >> v) want.push_back(v);
			chip.writeData(uint8_t(op));
			for(unsigned w : in) {
				chip.writeData(uint8_t(w));
				chip.writeData(uint8_t(w >> 8));
			}
			for(size_t k = 0; k < want.size(); k++) {
				unsigned lo = chip.readData(), hi = chip.readData();
				got.push_back(lo | hi << 8);
			}
			auto& s = stats[int(op)];
			s.second++;
			if(got == want) s.first++;
			else if(!firstMiss.count(int(op))) firstMiss[int(op)] = line;
		}
	}
	bool allExact = true;
	for(auto& [op, s] : stats) {
		std::printf("%02x: %ld/%ld exact%s%s\n", op, s.first, s.second,
			firstMiss.count(op) ? "   first miss: " : "", firstMiss.count(op) ? firstMiss[op].c_str() : "");
		if(s.first != s.second) allExact = false;
	}
	return strict && !allExact ? 1 : 0;
}
