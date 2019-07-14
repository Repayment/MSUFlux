#pragma once

#include "utilities/reaction.h"

#include <vector>
#include <string>
#include <algorithm>

std::vector<std::string> CreateFullMetaboliteList(const std::vector<Reaction> &reactions);

std::vector<std::string> CreateIncludedMetaboliteList(const std::vector<std::string> &metabolite_list,
                                                      const std::vector<std::string> &excluded_metabolites);