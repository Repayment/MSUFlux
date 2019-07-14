#include "solver.h"

#include <random>
#include "alglib/optimization.h"

#include "solver/solver_package.h"
#include "simulator/calculate_mids.h"


Solver* Solver::instance;


int Solver::iteration_total_;
int Solver::iteration_;

 std::vector<Reaction> Solver::reactions_;
 std::vector<Emu> Solver::measured_isotopes_;
 Matrix Solver::nullspace_;
 std::vector<EMUNetwork> Solver::networks_;
 std::vector<EmuAndMid> Solver::input_mids_;
 std::vector<Measurement> Solver::measurements_;
 int Solver::measurements_count_;

 int Solver::nullity_;

 alglib::real_1d_array Solver::free_fluxes_;
 alglib::real_1d_array Solver::lower_bounds_;
 alglib::real_1d_array Solver::upper_bounds_;

 alglib::minlmstate Solver::state;
 alglib::minlmreport Solver::report;

 std::vector<alglib::real_1d_array> Solver::allSolutions;


Solver::Solver(Problem &problem) {
    Solver::reactions_ = problem.reactions;
    measured_isotopes_ = problem.measured_isotopes;
    nullspace_ = problem.nullspace;
    networks_ = problem.networks;
    input_mids_ = problem.input_mids;
    measurements_ = problem.measurements;
    measurements_count_ = problem.measurements_count;
    nullity_ = nullspace_.cols();

    free_fluxes_.setlength(nullity_);
    lower_bounds_.setlength(nullity_);
    upper_bounds_.setlength(nullity_);

    iteration_ = 0;
    iteration_total_ = 10;
}


void Solver::Solve() {
    FillBoundVectors();
    SetOptimizationParameters();
    for (iteration_ = 0; iteration_ < iteration_total_; ++iteration_) {
        GenerateInitialPoints();
        alglib::minlmrestartfrom(state, free_fluxes_);

        alglib::real_1d_array newSolution = RunOptimization();

        allSolutions.emplace_back(newSolution);
    }
}


std::vector<alglib::real_1d_array> Solver::getResult() {
    return allSolutions;
}


Solver* Solver::getInstance(Problem &problem) {
    if (!instance) {
        instance = new Solver(problem);
    }

    return instance;
}

void Solver::FillBoundVectors() {
    const int reaction_total = reactions_.size();

    for (int i = 0; i < nullity_; ++i) {
        lower_bounds_[i] = reactions_[reaction_total - nullity_ + i].computed_lower_bound;
        upper_bounds_[i] = reactions_[reaction_total - nullity_ + i].computed_upper_bound;
    }
}

void Solver::SetOptimizationParameters() {
    alglib::ae_int_t maxits = 0;
    const double epsx = 0.00000000001;

    alglib::minlmcreatev(nullity_, measurements_count_, free_fluxes_, 0.0001, state);
    alglib::minlmsetcond(state, epsx, maxits);
    alglib::minlmsetbc(state, lower_bounds_, upper_bounds_);
}

void Solver::GenerateInitialPoints() {

    std::random_device randomizer;
    std::mt19937 random_source(randomizer());

    std::uniform_real_distribution<> get_random_point(0.0, 1.0);

    for (int i = 0; i < nullity_; ++i) {
        free_fluxes_[i] = lower_bounds_[i] + get_random_point(random_source) * (upper_bounds_[i] - lower_bounds_[i]);
    }

    PrintStartMessage();
}


void Solver::PrintStartMessage() {
    std::cout << "Start " << iteration_ << " iteration from: " << std::endl;
    for (int i = 0; i < nullity_; ++i) {
        std::cout << reactions_[reactions_.size() - nullity_ + i].name << " = " << free_fluxes_[i] << std::endl;
    }
    std::cout << std::endl;
}



alglib::real_1d_array Solver::RunOptimization() {

    alglib::minlmoptimize(state, CalculateResidual);

    alglib::real_1d_array final_free_fluxes;
    alglib::minlmresults(state, final_free_fluxes, report);

    std::vector<Flux> final_all_fluxes = CalculateAllFluxesFromFree(final_free_fluxes);


    std::vector<EmuAndMid> simulated_mids  = CalculateMids(final_all_fluxes,
                                                          networks_,
                                                          input_mids_,
                                                          measured_isotopes_);

    alglib::real_1d_array residuals;
    residuals.setlength(measurements_count_);

    Fillf0Array(residuals, simulated_mids);

    double best_ssr = GetSSR(residuals);

    PrintFinalMessage(final_free_fluxes, best_ssr);

    return final_free_fluxes;
}



void Solver::CalculateResidual(const alglib::real_1d_array &free_fluxes,
                               alglib::real_1d_array &residuals, void* ptr) {

    std::vector<Flux> calculated_fluxes = CalculateAllFluxesFromFree(free_fluxes);

    std::vector<EmuAndMid> simulated_mids = CalculateMids(calculated_fluxes, networks_,
                                                          input_mids_, measured_isotopes_);

    Fillf0Array(residuals, simulated_mids);
}




std::vector<Flux> Solver::CalculateAllFluxesFromFree(const alglib::real_1d_array &free_fluxes_alglib) {
    Eigen::VectorXd free_fluxes_eigen = GetEigenVectorFromAlgLibVector(free_fluxes_alglib);
    Matrix all_fluxes_matrix = nullspace_ * free_fluxes_eigen;
    std::vector<Flux> all_fluxes(reactions_.size());

    // non metabolite balance reactions
    const int real_reactions_total = all_fluxes_matrix.rows();

    // metabolite balance
    const int fake_reactions_total = reactions_.size() - all_fluxes_matrix.rows();

    for (int i = 0; i < real_reactions_total; ++i) {
        all_fluxes[reactions_.at(reactions_.size() - real_reactions_total + i).id] = all_fluxes_matrix(i, 0);
    }

    // fill const fake fluxes
    for (int i = 0; i < fake_reactions_total; ++i) {
        all_fluxes[reactions_.at(i).id] = 1;
    }

    return all_fluxes;
}


Eigen::VectorXd Solver::GetEigenVectorFromAlgLibVector(const alglib::real_1d_array &alglib_vector) {
    Eigen::VectorXd eigen_vector(alglib_vector.length());
    for (int i = 0; i < alglib_vector.length(); ++i) {
        eigen_vector(i) = alglib_vector[i];
    }

    return eigen_vector;
}

void Solver::Fillf0Array(alglib::real_1d_array &residuals, const std::vector<EmuAndMid> &simulated_mids) {
    int total_residuals = 0;
    for (int isotope = 0; isotope < simulated_mids.size(); ++isotope) {
        for (int mass_shift = 0; mass_shift < simulated_mids[isotope].mid.size(); ++mass_shift) {
            residuals[total_residuals] = simulated_mids[isotope].mid[mass_shift];
            residuals[total_residuals] -= (measurements_[isotope].mid[mass_shift]);
            residuals[total_residuals] /= 1 + measurements_[isotope].errors[mass_shift];
            ++total_residuals;
        }
    }
}


double Solver::GetSSR(const alglib::real_1d_array &residuals) {
    double answer = 0.0;
    for (int measurement = 0; measurement < measurements_count_; ++measurement) {
        answer += residuals(measurement) * residuals(measurement);
    }
    return answer;
}

void Solver::PrintFinalMessage(const alglib::real_1d_array &free_fluxes,
                       const double ssr) {
    std::cout << "Finish at: " << std::endl;

    for (int i = 0; i < free_fluxes.length(); ++i) {
        std::cout << reactions_[reactions_.size() - free_fluxes.length() + i].name <<
                  " = " << free_fluxes[i] << std::endl;
    }
    std::cout << "with SSR: " << ssr << " in " << report.iterationscount << " steps." << std::endl << std::endl;
}