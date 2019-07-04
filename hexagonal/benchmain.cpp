/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <benchmark/benchmark.h>

#include <glog/logging.h>

#include <cstdlib>

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  benchmark::Initialize (&argc, argv);
  benchmark::RunSpecifiedBenchmarks ();

  return EXIT_SUCCESS;
}
