#include "flux_balance_analysis.h"
#include "../math/math_utilites.h"
#include "../utilities/reaction_struct.h"
#include "../utilities/linear_problem.h"
#include "../glpk/glpk.h"

#include <vector>
#include <string>
#include <cmath>

// should think about functions parameters
// should think twice about Class LinearProblem design
std::vector<Flux> EstablishInitialFluxes(const Matrix &stoichiometry_matrix,
                                         const std::vector<Reaction> &reactions,
                                         const std::vector<std::string> &included_metabolites) {
    glp_term_out(GLP_OFF); //disable terminal output
    LinearProblem initialize_fluxes(stoichiometry_matrix.size());
    CreateLinearProblem(initialize_fluxes, stoichiometry_matrix, reactions, included_metabolites);
    glp_set_prob_name(initialize_fluxes, "Initialize Fluxes");
    glp_set_obj_dir(initialize_fluxes, GLP_MIN);

    int current_reaction_index = 1;
    for (const Reaction &reaction : reactions) {
        if (reaction.type != ReactionType::IsotopomerBalance) {
            glp_set_obj_coef(initialize_fluxes, current_reaction_index, 1.0);
        }
    }

    glp_simplex(initialize_fluxes, NULL);
    std::vector<Flux> initial_fluxes;
    for (int i = 1; i < stoichiometry_matrix.cols(); ++i) {
        initial_fluxes.push_back(glp_get_col_prim(initialize_fluxes, i));
    }

    return initial_fluxes;
}

std::vector<FluxVariability> EstablishAllFluxRanges(const Matrix &stoichiometry_matrix,
                                                    const std::vector<Reaction> &reactions,
                                                    const std::vector<std::string> &included_metabolites) {
    std::vector<FluxVariability> flux_ranges;
    int current_reaction_index = 1;
    for (const Reaction &reaction : reactions) {
        if (reaction.type != ReactionType::IsotopomerBalance) {
            flux_ranges.emplace_back(EstablishFluxRange(current_reaction_index, stoichiometry_matrix,
                                                        reactions, included_metabolites));
            ++current_reaction_index;
        }
    }

    return flux_ranges;
}

FluxVariability EstablishFluxRange(int reaction_index,
                                   const Matrix &stoichiometry_matrix,
                                   const std::vector<Reaction> &reactions,
                                   const std::vector<std::string> &included_metabolites) {
    Flux lower_bound = EstablishExtremeFlux(reaction_index, stoichiometry_matrix,
                                            reactions, included_metabolites, false);

    Flux upper_bound = EstablishExtremeFlux(reaction_index, stoichiometry_matrix,
                                            reactions, included_metabolites, true);

    FluxVariability flux_range;
    flux_range.lower_bound = lower_bound;
    flux_range.upper_bound = upper_bound;

    return flux_range;
}

Flux EstablishExtremeFlux(int reaction_index,
                          const Matrix &stoichiometry_matrix,
                          const std::vector<Reaction> &reactions,
                          const std::vector<std::string> &included_metabolites,
                          bool maximize) {
    glp_term_out(GLP_OFF);
    LinearProblem bound(stoichiometry_matrix.size());
    CreateLinearProblem(bound, stoichiometry_matrix, reactions, included_metabolites);
    glp_set_prob_name(bound, "Initialize Fluxes");
    const int objective_direction = maximize ? GLP_MAX : GLP_MIN;
    glp_set_obj_dir(bound, objective_direction);

    int current_reaction_index = 1;
    for (const auto &reaction : reactions) {
        if (reaction.type != ReactionType::IsotopomerBalance) {
            glp_set_obj_coef(bound, current_reaction_index, 0.0);
        }
    }

    glp_set_obj_coef(bound, reaction_index, 1.0);
    glp_simplex(bound, NULL);
    return glp_get_col_prim(bound, reaction_index);
}

// Prepare LinearProblem with constraints from the stoichiometry matrix
// and bounds from the model
//
void CreateLinearProblem(LinearProblem &linear_problem, const Matrix &stoichiometry_matrix,
                         const std::vector<Reaction> &reactions,
                         const std::vector<std::string> &included_metabolites) {
    PrepareMatrixForGLPK(stoichiometry_matrix, linear_problem);

    glp_add_rows(linear_problem, stoichiometry_matrix.rows());

    // mass balance constraints
    for (int metabolite_index = 0; metabolite_index < included_metabolites.size(); ++metabolite_index) {
        glp_set_row_name(linear_problem, metabolite_index + 1, included_metabolites[metabolite_index].c_str());
        glp_set_row_bnds(linear_problem, metabolite_index + 1, GLP_FX, 0.0, 0.0);
    }

    glp_add_cols(linear_problem, stoichiometry_matrix.cols());

    // adding bounds
    int current_reaction_index = 1;
    for (const Reaction &reaction : reactions) {
        if (reaction.type != ReactionType::IsotopomerBalance) {
            glp_set_col_name(linear_problem, current_reaction_index, reaction.name.c_str());

            if (std::isnan(reaction.basis)) {
                glp_set_col_bnds(linear_problem, current_reaction_index, GLP_DB,
                                 reaction.lower_bound, reaction.upper_bound);
            } else {
                double shift{0.0};
                auto glp_parameter = GLP_FX; // because GLP_DB not work, when LB = UB
                if (!std::isnan(reaction.deviation)) {
                    shift = reaction.deviation;
                    // There is computation nuance. Should we use std::fabs(shift) > 0.0 in bounds testing?
                    if (std::fabs(shift) > 0.0) {
                        glp_parameter = GLP_DB;
                    }
                }
                glp_set_col_bnds(linear_problem, current_reaction_index, glp_parameter,
                                 reaction.basis - shift, reaction.basis + shift);
            }

            ++current_reaction_index;
        }
    }

    glp_load_matrix(linear_problem, stoichiometry_matrix.size(),
                    linear_problem.ia, linear_problem.ja, linear_problem.ar);

}

void PrepareMatrixForGLPK(const Matrix &matrix,
                          LinearProblem &linear_problem) {
    // see glpk Reference Manual pg. 11
    for (int i = 0; i < matrix.rows(); ++i) {
        for (int j = 0; j < matrix.cols(); ++j) {
            int current_index = i * matrix.cols() + j + 1;
            linear_problem.ia[current_index] = i + 1;
            linear_problem.ja[current_index] = j + 1;
            linear_problem.ar[current_index] = matrix(i, j);
        }
    }
}