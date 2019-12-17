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

% Relevant numbers of prizes for which we need corresponding odds.
nums = [1, 2, 3, 5, 10, 30, 40, 50, 2000];

% Computes the chance denominator we need to achieve a certain probability
% of finding at least x prizes in n tries.
function invChance = findInvChance (n, targetChance, x)
  % binocdf(x, n, p) gives the chance of having <= x prizes found
  % with n trials and probability p.  We want to get the chance of >= x,
  % so we need 1 - binocdf(x-1, n, p).
  getChanceDiff = @(p) (1 - binocdf (x - 1, n, p)) - targetChance;

  p = fzero (getChanceDiff, [0, 1]);
  invChance = 1 / p;
endfunction

for i = nums
  printf ("%d: 1 / %.0f\n", i, findInvChance (n, targetChance, i));
endfor

% For the gold/silver/bronze prizes, we have other parameters.
% Note that the odds computed here are just for reference, and the
% actual values in the code have been chosen manually.
printf ("Gold:   1 / %.0f\n", findInvChance (400e3, 0.15, 3));
printf ("Silver: 1 / %.0f\n", findInvChance (400e3, 0.4, 5));
printf ("Bronze: 1 / %.0f\n", findInvChance (400e3, 0.9, 10));
