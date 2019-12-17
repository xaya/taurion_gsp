#   GSP for the Taurion blockchain game
#   Copyright (C) 2019  Autonomous Worlds Ltd
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

# GNU Octave script that computes the probability p we need in a binomial
# distribution if we want 90% chance of at least n "successes" with 150k trials.
# This defines the odds we put for the extra prizes in the Taurion competition.

clear ("all");
pkg load statistics;

n = 150e3;
targetChance = 0.9;

prizes = {
  {"shr", 60},
  {"spirit clash", 730},
  {"dio", 50},
  {"1up", 20},
  {"battle racers", 3},
  {"divi", 20},
  {"dft", 50},
  {"9la necklace", 30},
  {"9la miner", 10},
  {"9la yellow", 10},
  {"9la horned", 10},
  {"snails", 20},
};

for i = 1 : length (prizes)
  % binocdf(x, n, p) gives the chance of having <= x prizes found
  % with n trials and probability p.  We want to get the chance of >= x,
  % so we need 1 - binocdf(x-1, n, p).
  getChanceDiff = @(p) (1 - binocdf (prizes{i}{2} - 1, n, p)) - targetChance;

  p = fzero (getChanceDiff, [0, 1]);
  invChance = 1 / p;
  printf ("%20s: 1 / %.0f\n", prizes{i}{1}, invChance);
endfor
