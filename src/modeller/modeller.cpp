#include "modeller/modeller.h"

#include <math.h>
#include <iostream>

#include "utilities/matrix.h"
#include "modeller/create_emu_networks.h"
#include "modeller/calculate_input_mid.h"
#include "modeller/create_emu_reactions.h"
#include "modeller/create_emu_list.h"
#include "modeller/create_metabolite_list.h"
#include "modeller/create_stoichiometry_matrix.h"
#include "modeller/create_nullspace.h"
#include "modeller/calculate_flux_bounds.h"
#include "modeller/check_model.h"

#include "utilities/debug_utills/debug_prints.h"

namespace khnum {
Modeller::Modeller(ParserResults parser_results) {
    reactions_ = std::move(parser_results.reactions);
    measured_isotopes_ = std::move(parser_results.measured_isotopes);
    measurements_ = std::move(parser_results.measurements);
    excluded_metabolites_ = std::move(parser_results.excluded_metabolites);
    input_substrate_ = std::move(parser_results.input_substrate);
}


void Modeller::CalculateInputSubstrateMids() {
    all_emu_reactions_ = modelling_utills::CreateAllEmuReactions(reactions_, measured_isotopes_);
    input_emu_list_ = modelling_utills::CreateInputEmuList(all_emu_reactions_, input_substrate_);
    input_substrate_mids_ = modelling_utills::CalculateInputMid(input_substrate_, input_emu_list_);
}


void Modeller::CreateEmuNetworks() {
    emu_networks_ = modelling_utills::CreateEmuNetworks(all_emu_reactions_, input_emu_list_, measured_isotopes_);
}


void Modeller::CalculateFluxBounds() {
    modelling_utills::CalculateFluxBounds(reactions_, stoichiometry_matrix_);
}


void Modeller::CreateNullspaceMatrix() {
    std::vector<std::string> full_metabolite_list = modelling_utills::CreateFullMetaboliteList(reactions_);
    std::vector<std::string> included_metabolites = modelling_utills::CreateIncludedMetaboliteList(full_metabolite_list,
                                                                                 excluded_metabolites_);

    stoichiometry_matrix_ = modelling_utills::CreateStoichiometryMatrix(reactions_, included_metabolites);
    nullspace_ = modelling_utills::GetNullspace(stoichiometry_matrix_, reactions_);
}


void Modeller::CalculateMeasurementsCount() {
    measurements_count_ = 0;

    for (const Measurement &measurement : measurements_) {
        measurements_count_ += measurement.mid.size();
    }
}


void Modeller::CheckModelForErrors() {
    modelling_utills::CheckMeasurementsMID(measurements_);
}

Problem Modeller::GetProblem() {
    Problem problem;
    problem.reactions = reactions_;
    problem.measured_isotopes = measured_isotopes_;
    problem.nullspace = nullspace_;
    problem.networks = emu_networks_;
    problem.input_substrate_mids = input_substrate_mids_;
    problem.measurements = measurements_;
    problem.measurements_count = measurements_count_;

    return problem;
}
} // namespace khnum