#pragma once

#include "stdafx.h"
#include "params.hpp"
#include "app_var.hpp"
#include "MultiGridSolver/multigrid_solver.h"

template<typename T>
class ChiSolver : public MultiGridSolver<3, T>
{
private:
    // Parameters
    T a;    // scale factor
    T a_3;  // prec-compute a^3
    T D;    // pre-compute D(a)

    const T n;        // Hu-Sawicki paramater
    const T chi_0;    // 2*beta*Mpl*phi_scr
    const T chi_prefactor_0; // dimensionless prefactor to poisson equation at a = 1
    T chi_prefactor;

    bool conv_stop = false;

public:
    // Constructors
    ChiSolver(unsigned int N, const Sim_Param& sim, bool verbose = true) : ChiSolver(N, 2, sim, verbose) {}
    ChiSolver(unsigned int N, int Nmin, const Sim_Param& sim, bool verbose = true);
    void set_time(T a, const Cosmo_Param& cosmo);

    // The dicretized equation L(phi)
    T  l_operator(unsigned int level, std::vector<unsigned int>& index_list, bool addsource) override;

    // Differential of the L operator: dL_{ijk...}/dphi_{ijk...}
    T dl_operator(unsigned int level, std::vector<unsigned int>& index_list) override;

    // Criterion for defining convergence
    bool check_convergence() override;

    // set initial guess to bulk value, need to set time and add rho before call to this function
    void set_initial_guess();

    // get chi_bulk for given overdensity
    T chi_min(T delta) const;
};

/**
 * @class:	App_Var_chi
 * @brief:	class containing variables for chameleon gravity
 */

typedef long double CHI_PREC_t;
 
class App_Var_chi: public App_Var<Particle_v<FTYPE_t>>
{
public:
	// CONSTRUCTORS & DESTRUCTOR
	App_Var_chi(const Sim_Param &sim, std::string app_str);

	// VARIABLES
    ChiSolver<CHI_PREC_t> sol;
    MultiGrid<3, CHI_PREC_t> drho;
    std::vector<Mesh> chi_force;

    // METHODS
    void print_output();
    void solve(FTYPE_t a);

protected:
    void save_drho_from_particles(Mesh& aux_field);
};