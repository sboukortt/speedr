# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Sami Boukortt
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

project(
	'speedr',
	'cpp',
	default_options: ['cpp_std=c++17'],
	version: '1.0.1',
	license: 'Apache-2.0',
	license_files: 'COPYING.txt',
)

sndfile_dep = dependency('sndfile')
hwy_dep = dependency('libhwy', version: '1.3.0', default_options: [
	'contrib=disabled',
	'examples=disabled',
	'tests=disabled',
])
omp_dep = dependency('openmp', required: false)
cli11_dep = dependency('CLI11')

speedr = executable(
	'speedr',
	'compute_dr.h',
	'compute_dr.cpp',
	'main.cpp',
	dependencies: [sndfile_dep, hwy_dep, omp_dep, cli11_dep],
	install: true,
)
