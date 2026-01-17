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
#include <vector>

#include <CLI/CLI.hpp>
#include <sndfile.hh>

#include "compute_dr.h"

namespace {

struct Rating {
	enum class RatingType {
		kMonoRating,
		kStereoRating,
	} rating_type;
	union {
		float mono_rating;
		struct { float left, right; } stereo_rating;
	};

	float final_rating;
};

Rating ComputeRating(SndfileHandle& input) {
	if (input.channels() == 1) {
		const float dr = speedr::ComputeMonoDR(input);
		return {
			.rating_type = Rating::RatingType::kMonoRating,
			.mono_rating = dr,
			.final_rating = std::round(dr)
		};
	}
	else {
		const std::pair<float, float> raw_rating = speedr::ComputeStereoDR(input);
		const auto [left_dr, right_dr] = raw_rating;
		return {
			.rating_type = Rating::RatingType::kStereoRating,
			.stereo_rating = {left_dr, right_dr},
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

	#pragma omp parallel for
	for (auto& track: tracks) {
		std::get<Rating>(track) = ComputeRating(std::get<SndfileHandle>(track));
	}

	float album_rating = 0.f;
	for (const auto& [filename, handle, rating]: tracks) {
		std::cout << filename << ":" << std::endl;
		switch (rating.rating_type) {
			case Rating::RatingType::kMonoRating:
				std::cout << "\tRaw DR: " << rating.mono_rating << std::endl;
				break;
			case Rating::RatingType::kStereoRating: {
				const auto [left_dr, right_dr] = rating.stereo_rating;
				std::cout << "\tLeft DR: " << left_dr << std::endl;
				std::cout << "\tRight DR: " << right_dr << std::endl;
				break;
			}
		}
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
