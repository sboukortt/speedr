// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Sami Boukortt
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <variant>
#include <vector>

#include <CLI/CLI.hpp>
#include <sndfile.hh>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "compute_dr.h"

namespace {

struct Rating {
	struct MonoRating {
		float value;
	};
	struct StereoRating {
		float left, right;
	};
	std::variant<MonoRating, StereoRating> raw_rating;

	float final_rating;
};

Rating ComputeRating(SndfileHandle& input) {
	if (input.channels() == 1) {
		const float dr = speedr::ComputeMonoDR(input);
		return {
			.raw_rating = Rating::MonoRating{dr},
			.final_rating = std::round(dr)
		};
	}
	else {
		const std::pair<float, float> raw_rating = speedr::ComputeStereoDR(input);
		const auto [left_dr, right_dr] = raw_rating;
		return {
			.raw_rating = Rating::StereoRating{left_dr, right_dr},
			.final_rating = std::round((left_dr + right_dr) / 2)
		};
	}
}

}

int main(int argc, char** argv) {
	CLI::App app("SpeeDR - dynamic range calculator");
	argv = app.ensure_utf8(argv);
	std::vector<std::string> filenames;
	app.add_option("filename", filenames, "Files to analyse")->required();
	CLI11_PARSE(app, argc, argv);

	std::vector<std::tuple<std::string_view, SndfileHandle, Rating>> tracks;

	for (const std::string& filename: filenames) {
#ifdef _WIN32
		SndfileHandle input(CLI::widen(filename).c_str());
#else
		SndfileHandle input(filename);
#endif
		if (!input.rawHandle()) {
			std::cerr << "Failed to open " << filename << " for audio decoding: " << input.strError() << std::endl;
			return EXIT_FAILURE;
		}
		if (input.channels() > 2) {
			std::cerr << "This metric is only designed for mono and stereo input (" << filename << " has " << input.channels() << " channels)" << std::endl;
			return EXIT_FAILURE;
		}

		tracks.emplace_back(filename, std::move(input), Rating());
	}

#ifdef _OPENMP
	const int num_threads = std::min<int>(tracks.size(), omp_get_max_threads());
#endif

	#pragma omp parallel for num_threads(num_threads)
	for (auto& track: tracks) {
		std::get<Rating>(track) = ComputeRating(std::get<SndfileHandle>(track));
	}

	float album_rating = 0.f;
	for (const auto& [filename, handle, rating]: tracks) {
		std::cout << filename << ":" << std::endl;
		struct RatingPrinter {
			void operator()(const Rating::MonoRating& rating) const {
				std::cout << "\tRaw DR: " << rating.value << std::endl;
			}
			void operator()(const Rating::StereoRating& rating) const {
				std::cout << "\tLeft DR: " << rating.left << std::endl;
				std::cout << "\tRight DR: " << rating.right << std::endl;
			}
		};
		std::visit(RatingPrinter{}, rating.raw_rating);
		if (std::isfinite(rating.final_rating)) {
			std::cout << "\tTrack rating: DR" << rating.final_rating << std::endl;
		}
		else {
			std::cout << "\tTrack rating: N/A" << std::endl;
		}
		album_rating += rating.final_rating;
	}

	if (tracks.size() > 1) {
		album_rating = std::round(album_rating / tracks.size());
		std::cout << std::endl;
		if (std::isfinite(album_rating)) {
			std::cout << "Album rating: DR" << album_rating << std::endl;
		}
		else {
			std::cout << "Album rating: N/A" << std::endl;
		}
	}
}
