#include "calculate_mids.h"

#include <vector>
#include <algorithm>
#include <iostream>
#include <exception>

#include "utilities/emu.h"
#include "utilities/emu_and_mid.h"
#include "utilities/reaction.h"
#include "utilities/matrix.h"


std::vector<EmuAndMid> CalculateMids(const std::vector<Flux>  &fluxes,
                                     const std::vector<EMUNetwork> &networks,
                                     std::vector<EmuAndMid> known_mids,
                                     const std::vector<Emu> &measured_isotopes) {
    for (const EMUNetwork &network : networks) {
        SolveOneNetwork(fluxes, network, known_mids);
    }

    return SelectMeasuredMID(known_mids, measured_isotopes);
}


int FindNetworkSize(const EMUNetwork &network) {
    int current_size = 0;
    for (const bool state : network[0].right.emu.atom_states) {
        current_size += static_cast<int>(state);
    }
    return current_size;
}


// Return vector of simulated MIDs of measured_isotopes
std::vector<EmuAndMid> SelectMeasuredMID(const std::vector<EmuAndMid> &known_mids,
                                         const std::vector<Emu> &measured_isotopes) {
    std::vector<EmuAndMid> measured_mids;

    for (const Emu &measured_isotope : measured_isotopes) {
        auto position = find_if(known_mids.begin(),
                                known_mids.end(),
                                [&measured_isotope](const EmuAndMid &emu) {
                                    return emu.emu == measured_isotope;
                                });

        if (position != known_mids.end()) {
            measured_mids.push_back(*position);
        } else {
            throw std::runtime_error("There is measured isotope which haven't computed through metabolic network");
        }
    }

    return measured_mids;
}


void SolveOneNetwork(const std::vector<Flux> &fluxes,
                     const EMUNetwork &network,
                     std::vector<EmuAndMid> &known_mids) {

    const int current_size = FindNetworkSize(network);

    // Solve AX = BY equation
    // See Antoniewitcz 2007

    // EMUs which MIDs are unknown
    // for the X matrix
    std::vector<Emu> unknown_emus;

    // EMUs which MIDs are known
    // for the Y matrix
    std::vector<EmuAndMid> known_emus;

    FillEMULists(unknown_emus, known_emus, network, known_mids);

    Matrix A = Matrix::Zero(unknown_emus.size(), unknown_emus.size());
    Matrix B = Matrix::Zero(unknown_emus.size(), known_emus.size());
    Matrix Y(known_emus.size(), current_size + 1);

    FormYMatrix(Y, known_emus, current_size);
    FormABMatrices(A, B, network, known_emus, unknown_emus, fluxes, known_mids);

    Matrix BY = B * Y;
    Matrix X = A.colPivHouseholderQr().solve(BY);

    AppendNewMIDs(X,unknown_emus, known_mids, current_size);
}

void FillEMULists(std::vector<Emu> &unknown_emus,
                  std::vector<EmuAndMid> &known_emus,
                  const EMUNetwork &network,
                  const std::vector<EmuAndMid> &known_mids) {

    // Fills known_emus and unknown_emus
    for (const EMUReaction &reaction : network) {

        // checking the left side
        if (reaction.left.size() == 1) {
            const Mid *mid = GetMID(reaction.left[0].emu, known_mids);
            if (mid) {
                EmuAndMid new_known;
                new_known.emu = reaction.left[0].emu;
                new_known.mid = *mid;
                known_emus.push_back(new_known);
            } else {
                unknown_emus.push_back(reaction.left[0].emu);
            }
        } else {
            EmuAndMid convolution = ConvolveEMU(reaction.left, known_mids);
            known_emus.push_back(convolution);
        }

        // checking the right side
        const Mid *mid = GetMID(reaction.right.emu, known_emus);
        if (mid) {
            EmuAndMid new_known;
            new_known.emu = reaction.right.emu;
            new_known.mid = *mid;
            known_emus.push_back(new_known);
        } else {
            unknown_emus.push_back(reaction.right.emu);
        }
    }

    // delete repeated emus
    std::sort(known_emus.begin(), known_emus.end());
    known_emus.erase(std::unique(known_emus.begin(), known_emus.end()), known_emus.end());

    std::sort(unknown_emus.begin(), unknown_emus.end());
    unknown_emus.erase(std::unique(unknown_emus.begin(), unknown_emus.end()), unknown_emus.end());
}


void FormYMatrix(Matrix &Y,
                 const std::vector<EmuAndMid> &known_emus,
                 const int current_size) {
    for (int known_emu_index = 0; known_emu_index < known_emus.size(); ++known_emu_index) {
        for (int mass_shift = 0; mass_shift < current_size + 1; ++mass_shift) {
            Y(known_emu_index, mass_shift) = known_emus[known_emu_index].mid[mass_shift];
        }
    }
}

void FormABMatrices(Matrix &A, Matrix &B,
                    const EMUNetwork &network,
                    const std::vector<EmuAndMid> &known_emus,
                    const std::vector<Emu> &unknown_emus,
                    const std::vector<Flux> &fluxes,
                    const std::vector<EmuAndMid> &known_mids) {
    for (const EMUReaction &reaction : network) {
        EmuSubstrate substrate;
        if (reaction.left.size() > 1) {
            EmuAndMid convolution = ConvolveEMU(reaction.left, known_mids);
            substrate.emu = convolution.emu;
            substrate.coefficient = 1.0;
        } else {
            substrate = reaction.left[0];
        }

        if (!IsEMUKnown(substrate.emu, known_emus)) {
            // they are both unknown
            int position_of_substrate = FindUnknownEMUsPosition(substrate.emu, unknown_emus);
            int position_of_product = FindUnknownEMUsPosition(reaction.right.emu, unknown_emus);
            A(position_of_product, position_of_product) += (-reaction.right.coefficient) * fluxes.at(reaction.id);

            // Why does it multiple by product coefficient? Shouldn't it be substrate coefficient?
            A(position_of_product, position_of_substrate) += reaction.right.coefficient * fluxes.at(reaction.id);
        } else {
            // Product is unknown, Substrate is known

            int position_of_substrate = FindKnownEMUsPosition(substrate.emu, known_emus);
            int position_of_product = FindUnknownEMUsPosition(reaction.right.emu, unknown_emus);

            A(position_of_product, position_of_product) += (-reaction.right.coefficient) * fluxes.at(reaction.id);

            B(position_of_product, position_of_substrate) +=
                    (-substrate.coefficient) * fluxes.at(reaction.id);
        }

    }
}


void AppendNewMIDs(const Matrix &X,
                   const std::vector<Emu> &unknown_emus,
                   std::vector<EmuAndMid> &known_mids,
                   const int current_size) {

    for (int previously_unknown_index = 0; previously_unknown_index < unknown_emus.size(); ++previously_unknown_index) {
        EmuAndMid new_known_emu;
        new_known_emu.emu = unknown_emus[previously_unknown_index];
        for (int mass_shift = 0; mass_shift < current_size + 1; ++mass_shift) {
            new_known_emu.mid.push_back(X(previously_unknown_index, mass_shift));
        }

        known_mids.push_back(new_known_emu);
    }

}


bool IsEMUKnown(const Emu &emu,
                const std::vector<EmuAndMid> known_emus) {
    auto position = find_if(known_emus.begin(),
                            known_emus.end(),
                            [&emu](const EmuAndMid &known_mid) {
                                return known_mid.emu == emu;
                            });

    return position != known_emus.end();
}

int FindUnknownEMUsPosition(const Emu &emu,
                            const std::vector<Emu> unknown_emus) {
    auto position = find(unknown_emus.begin(),
                         unknown_emus.end(),
                         emu);

    return position - unknown_emus.begin();
}

int FindKnownEMUsPosition(const Emu &emu,
                          const std::vector<EmuAndMid> known_emus) {
    auto position = find_if(known_emus.begin(),
                            known_emus.end(),
                            [&emu](const EmuAndMid &known_mid) {
                                return known_mid.emu == emu;
                            });

    return position - known_emus.begin();
}

EmuAndMid ConvolveEMU(const EmuReactionSide &convolve_reaction,
                      const std::vector<EmuAndMid> &known_mids) {
    EmuAndMid convolve_result;
    convolve_result.mid = Mid(1, 1.0);
    for (const EmuSubstrate &emu : convolve_reaction) {
        convolve_result.emu.name += emu.emu.name;
        for (const bool &state : emu.emu.atom_states) {
            convolve_result.emu.atom_states.push_back(state);
        }
        Mid new_mid = *GetMID(emu.emu, known_mids);
        convolve_result.mid = convolve_result.mid * new_mid;
    }

    return convolve_result;
}

const Mid *GetMID(const Emu &emu,
                  const std::vector<EmuAndMid> &known_mids) {
    auto position = find_if(known_mids.begin(),
                            known_mids.end(),
                            [&emu](const EmuAndMid &known_mid) {
                                return known_mid.emu == emu;
                            });

    if (position == known_mids.end()) {
        return nullptr;
    } else {
        return &(position->mid);
    }
}

