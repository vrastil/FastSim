/**
 * @file:	params.cpp
 * @brief:	various parameter classes definitions
 */

#include <omp.h>
#include "config.h"
#include "params.hpp"
#include "core_cmd.h"
#include "core_out.h"
#include "core_mesh.h"
#include "core_power.h"
#include <ccl_error.h>
#include <json.hpp>
#include <iomanip>

using json = nlohmann::json;

/*****************************//**
 * PRIVATE FUNCTIONS DEFINITIONS *
 *********************************/
namespace{

// convert to pyccl transfer_function_types keys
const std::map<std::string, transfer_function_t> transfer_function_method = {
    {"emulator", ccl_emulator},
    {"eisenstein_hu", ccl_eisenstein_hu},
    {"bbks", ccl_bbks},
    {"boltzmann_class", ccl_boltzmann_class},
    {"boltzmann_camb", ccl_boltzmann_camb}
};
// convert to pyccl matter_power_spectrum_types keys
const std::map<std::string, matter_power_spectrum_t> matter_power_spectrum_method = {
    {"linear", ccl_linear},
    {"halofit", ccl_halofit},
    {"halo_model", ccl_halo_model},
    {"emu", ccl_emu}
};
// convert to pyccl mass_function_types keys
const std::map<std::string, mass_function_t> mass_function_method = {
    {"tinker", ccl_tinker},
    {"tinker10", ccl_tinker10},
    {"watson", ccl_watson},
    {"angulo", ccl_angulo},
    {"shethtormen", ccl_shethtormen}
};
// convert to pyccl baryons_power_spectrum keys
const std::map<std::string, baryons_power_spectrum_t> baryons_power_spectrum_method = {
    {"nobaryons", ccl_nobaryons},
    {"bcm", ccl_bcm}
};

/**
 * return first occurence of 'value' in std::std::map
 */
template<typename T, typename U>
T find_value(const std::map<T, U>& map, const U& value)
{
    for(auto x : map) if (x.second == value) return x.first;
    throw std::out_of_range("Value not found");
}

size_t get_seed()
{
    // random seed number
    size_t seed = (static_cast<long>(rand()) << (sizeof(int) * 8)) | rand();

    // check for NULL pointer
    if (char * job_id_str = std::getenv("SBATCH_JOBID"))
    {
        // check for succesfull conversion
        if (long job_id = atol(job_id_str))
        {
            seed *= job_id;
        }        
    }
    else
    {
        BOOST_LOG_TRIVIAL(debug) << "Enviromental variable 'SBATCH_JOBID' was not found.";
    }
    return seed;
}

} ///< end of anonymous namespace (private definitions)

/****************************//**
 * PUBLIC FUNCTIONS DEFINITIONS *
 ********************************/

// interaction with json files
void to_json(json& j, const Cosmo_Param& cosmo)
{
    j = json{
        {"index", cosmo.ns},
        {"sigma8", cosmo.sigma8},
        {"smoothing_k", cosmo.k2_G},
        {"Omega_c", cosmo.Omega_c()},
        {"Omega_b", cosmo.Omega_b},
        {"Omega_m", cosmo.Omega_m},
        {"h", cosmo.h}
    };
    
    j["transfer_function_method"] = find_value(transfer_function_method, cosmo.config.transfer_function_method);
    j["matter_power_spectrum_method"] = find_value(matter_power_spectrum_method, cosmo.config.matter_power_spectrum_method);
    j["mass_function_method"] = find_value(mass_function_method, cosmo.config.mass_function_method);
    j["baryons_power_spectrum_method"] = find_value(baryons_power_spectrum_method, cosmo.config.baryons_power_spectrum_method);
}

void from_json(const json& j, Cosmo_Param& cosmo)
{
    cosmo.ns = j.at("index").get<FTYPE_t>();
    cosmo.sigma8 = j.at("sigma8").get<FTYPE_t>();
    cosmo.k2_G = j.at("smoothing_k").get<FTYPE_t>();
    cosmo.Omega_b = j.at("Omega_b").get<FTYPE_t>();
    cosmo.Omega_m = j.at("Omega_m").get<FTYPE_t>();
    cosmo.h = j.at("h").get<FTYPE_t>();
    cosmo.H0 = cosmo.h * 100;
    
    std::string tmp;
    try{
        tmp = j.at("transfer_function_method").get<std::string>();
        cosmo.config.transfer_function_method = transfer_function_method.at(tmp);
    }catch(const json::out_of_range& oor){
        cosmo.config.transfer_function_method = ccl_boltzmann_class;
    }
    try{
        tmp = j.at("matter_power_spectrum_method").get<std::string>();
        cosmo.config.matter_power_spectrum_method = matter_power_spectrum_method.at(tmp);
    }catch(const json::out_of_range& oor){
        cosmo.config.matter_power_spectrum_method = ccl_halofit;
    }
    try{
        tmp = j.at("mass_function_method").get<std::string>();
        cosmo.config.mass_function_method = mass_function_method.at(tmp);
    }catch(const json::out_of_range& oor){
        cosmo.config.mass_function_method = ccl_tinker10;
    }
    try{
        tmp = j.at("baryons_power_spectrum_method").get<std::string>();
        cosmo.config.baryons_power_spectrum_method = baryons_power_spectrum_method.at(tmp);
    }catch(const json::out_of_range& oor){
        cosmo.config.baryons_power_spectrum_method = ccl_nobaryons;
    }
    cosmo.init();
}

void to_json(json& j, const Box_Opt& box_opt)
{
    j = json{
        {"mesh_num", box_opt.mesh_num},
        {"mesh_num_pwr", box_opt.mesh_num_pwr},
        {"Ng", box_opt.Ng},
        {"par_num", box_opt.par_num_1d},
        {"box_size", box_opt.box_size},
        {"mass_p_log", box_opt.mass_p_log}
    };
}

void from_json(const json& j, Box_Opt& box_opt)
{
    box_opt.mesh_num = j.at("mesh_num").get<size_t>();
    box_opt.mesh_num_pwr = j.at("mesh_num_pwr").get<size_t>();
    box_opt.par_num_1d = j.at("par_num").get<size_t>();
    box_opt.box_size = j.at("box_size").get<FTYPE_t>();
}

void to_json(json& j, const Integ_Opt& integ_opt)
{
    j = json{
        {"redshift", integ_opt.z_in},
        {"redshift_0", integ_opt.z_out},
        {"time_step", integ_opt.db}
    };
}

void from_json(const json& j, Integ_Opt& integ_opt)
{
    integ_opt.z_in = j.at("redshift").get<FTYPE_t>();
    integ_opt.z_out = j.at("redshift_0").get<FTYPE_t>();
    integ_opt.db = j.at("time_step").get<FTYPE_t>();

    integ_opt.init();
}

void to_json(json& j, const App_Opt& app_opt)
{
    j = json{
        {"viscosity", app_opt.nu_dim},
        {"cut_radius", app_opt.rs}
    };
}

void from_json(const json& j, App_Opt& app_op)
{
    app_op.nu_dim = j.at("viscosity").get<FTYPE_t>();
    app_op.nu = app_op.nu_dim;
    app_op.rs = j.at("cut_radius").get<FTYPE_t>();
}

void to_json(json& j, const Run_Opt& run_opt)
{
    j = json{
        {"num_thread", run_opt.nt},
        {"seed", run_opt.seed},
        {"phase", run_opt.phase},
        {"version", PROJECT_VERSION}
    };
}

void from_json(const json& j, Run_Opt& run_opt)
{
    run_opt.nt = j.at("num_thread").get<size_t>();
    run_opt.seed = j.at("seed").get<size_t>();
    run_opt.phase = j.at("phase").get<bool>();
    run_opt.init();
}

void to_json(json& j, const Out_Opt& out_opt)
{
    j = json{
        {"bins_per_decade", out_opt.bins_per_decade},
        {"points_per_10_Mpc", out_opt.points_per_10_Mpc},
        {"out_dir", out_opt.out_dir}
    };
}

void from_json(const json& j, Out_Opt& out_opt)
{
    out_opt.bins_per_decade = j.at("bins_per_decade").get<size_t>();
    out_opt.points_per_10_Mpc = j.at("points_per_10_Mpc").get<size_t>();
    out_opt.out_dir = j.at("out_dir").get<std::string>();
}

void to_json(json& j, const Chi_Opt& chi_opt)
{
    j = json{
        {"beta", chi_opt.beta},
        {"n", chi_opt.n},
        {"phi", chi_opt.phi},
        {"linear", chi_opt.linear},
    };
}

void from_json(const json& j, Chi_Opt& chi_opt)
{
    chi_opt.beta = j.at("beta").get<FTYPE_t>();
    chi_opt.n = j.at("n").get<FTYPE_t>();
    chi_opt.phi = j.at("phi").get<FTYPE_t>();

    // optional parameters (old runs do not have everything)
    try {chi_opt.linear = j.at("linear").get<bool>();}
    catch (const json::out_of_range& oor){chi_opt.linear = false;}
}

void to_json(json& j, const Test_Opt& test_opt)
{
    j = json{
        {"R_sphere", test_opt.R_sphere},
        {"rho_sphere", test_opt.rho_sphere},
        {"N_grid", test_opt.N_grid},
        {"N_min", test_opt.N_min},
        {"rho_b", test_opt.rho_b}
    };
}

void from_json(const json& j, Test_Opt& test_opt)
{
    test_opt.R_sphere = j.at("R_sphere").get<FTYPE_t>();
    test_opt.rho_sphere = j.at("rho_sphere").get<FTYPE_t>();
    test_opt.N_grid = j.at("N_grid").get<size_t>();
    test_opt.N_min = j.at("N_min").get<size_t>();
    test_opt.rho_b = j.at("rho_b").get<FTYPE_t>();
}

/**
 * @class:	Cosmo_Param
 * @brief:	class storing parameters for power spectrum
 */

static ccl_cosmology * ccl_cosmology_create_flat_lcdm(
    double Omega_c, double Omega_b, double h, double norm_pk, double n_s,
    ccl_configuration config, int *status)
{
    double Omega_k = 0.0;
    double Neff = 3.04;
    double w0 = -1.0;
    double wa = 0.0;
    double *mnu;
    double mnuval = 0.;  // a pointer to the variable is not kept past the lifetime of this function
    mnu = &mnuval;
    ccl_mnu_convention mnu_type = ccl_mnu_sum;

    ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k, Neff,
            mnu, mnu_type, w0, wa, h, norm_pk, n_s, -1, -1, -1, -1, NULL, NULL, status);

    ccl_cosmology * cosmo = ccl_cosmology_create(params, config);
    return cosmo;
}

Cosmo_Param::Cosmo_Param():
    // cosmo == NULL as indicator of uninitialization
    // config first initialize to default (in case new configuration options are added)
    config(default_config), cosmo(NULL) {}

void Cosmo_Param::init()
{
    // in case we call iniit once again, free cosmo first
    if(cosmo)
    {
        ccl_cosmology_free(cosmo);
        cosmo = NULL;
    }
    /// - basic quantities
    h = H0/100;

    /// - create flat LCDM cosmology
    int status = 0;
    ccl_set_error_policy(CCL_ERROR_POLICY_CONTINUE);
    ccl_set_debug_policy(CCL_DEBUG_MODE_OFF);
    cosmo = ccl_cosmology_create_flat_lcdm(Omega_c(), Omega_b, h, sigma8, ns, config, &status);
    if (status) throw std::runtime_error(cosmo->status_message);
    
    /// - compute value of growth factor normalization, use only when outside CCL range
    D_norm = norm_growth_factor(*this);

    /// - normalize power spectrum
    norm_pwr(*this);
}

Cosmo_Param::~Cosmo_Param()
{
    if(cosmo){
        ccl_cosmology_free(cosmo);
        cosmo = NULL;
    }
}

Cosmo_Param::operator void*() const
// GSL accepts only non-const pointers, case when passed const Cosmo_Param&
{
    return const_cast<Cosmo_Param*>(this);
}

void Run_Opt::init()
{
    if(nt == 0) nt = omp_get_max_threads();
    else omp_set_num_threads(nt);
    if(nt_fftw == 0) nt_fftw = nt;
    if (seed == 0){
        srand(time(NULL));
        seed = get_seed();
    } else mlt_runs = 1;
}

void Run_Opt::reset()
{
    mlt_runs = 0;
}

bool Run_Opt::is_ready()
{
    return (mlt_runs != 0);
}

bool Run_Opt::simulate()
{
    if (!pair || !phase)
    {
        mlt_runs--;
        seed = get_seed();
    }
    if (pair) phase = !phase;
    return mlt_runs;
}

void Box_Opt::init(const Cosmo_Param& cosmo)
{
    Ng = mesh_num / par_num_1d;
    Ng_pwr = mesh_num_pwr/par_num_1d;
    par_num = par_num_1d*par_num_1d*par_num_1d;
    mass_p_log = std::log10(2.78E11*pow(box_size/par_num_1d, 3)*cosmo.Omega_m/cosmo.h);
}

void Integ_Opt::init()
{
    a_in = 1/(z_in + 1);
	a_out = 1/(z_out + 1);
}

void Out_Opt::init()
{
    get_pk_extrap = print_corr|| print_extrap_pwr;
    get_pwr = get_pk_extrap || print_pwr;
    get_rho = get_pwr || print_dens;
}

void Comp_App::reset()
{
    ZA = TZA = FF = FP = AA = FP_pp = chi = chi_ff = false;
}

bool Comp_App::is_ready()
{
    return (ZA | TZA | FF | FP | PM | AA | FP_pp | chi | chi_ff);
}

void App_Opt::init(const Box_Opt& box_opt)
{
    a = rs / FTYPE_t(0.735);
    M = (int)(box_opt.mesh_num / rs);
    Hc = FTYPE_t(box_opt.mesh_num) / M;
    nu_dim = nu;
    nu /= pow2(box_opt.box_size/box_opt.mesh_num); // converting to dimensionless units
}

void Other_par::init(const Box_Opt& box_opt)
{
    FTYPE_t tmp = PI/box_opt.box_size;

    nyquist["analysis"] = tmp*box_opt.mesh_num_pwr;
    nyquist["potential"] = tmp*box_opt.mesh_num;
    nyquist["particle"] = tmp*box_opt.par_num_1d;
    k_print.lower = 2*tmp;
    k_print.upper = 2*tmp*box_opt.mesh_num_pwr;
    x_corr.lower = 0.1;
    x_corr.upper = 200;
}

/**
 * @class:	Sim_Param
 * @brief:	class storing simulation parameters
 */

Sim_Param::Sim_Param(int ac, const char* const av[])
{
	handle_cmd_line(ac, av, *this);//< throw if anything happend
    if (this->is_ready())
    {
        run_opt.init();
        cosmo.init();
        box_opt.init(cosmo);
        integ_opt.init();
        out_opt.init();
        app_opt.init(box_opt);
        other_par.init(box_opt);
    }
}

Sim_Param::Sim_Param(std::string file_name)
{
    try{
        Ifstream i(file_name);
        json j;
        i >> j;
        try{ run_opt = j.at("run_opt"); } // sim_param.json has run_opt
        catch(const json::out_of_range& oor){ // stack_info.json does not have run_opt
            run_opt.nt = 0; // max
            run_opt.seed = 0; // random
            run_opt.init();
        }

        from_json(j.at("cosmo"), cosmo); //< call explicitly, SWIG has some issues with json.hpp
        if ("TZA" == j.at("app"))
        {
            cosmo.truncated_pk = true;
        }

        box_opt = j.at("box_opt");
        box_opt.init(cosmo);

        integ_opt = j.at("integ_opt");
        try{ out_opt = j.at("out_opt"); } // new format of json files
        catch(const json::out_of_range& oor){ // old format does not store Out_Opt
            try {out_opt.out_dir = j.at("out_dir"); } // stack_info.json doesn`t store out_dir
            catch(const json::out_of_range& oor){out_opt.out_dir = "~/home/FASTSIM/output/"; } // do not need it, set to some default
            out_opt.bins_per_decade = 20;
            out_opt.points_per_10_Mpc = 10;
        }
        app_opt = j.at("app_opt");
        
        app_opt.init(box_opt);
        other_par.init(box_opt);

        try{
            chi_opt = j.at("chi_opt");
            comp_app.chi = (("CHI" == j.at("app")));
            comp_app.chi_ff = (("CHI_FF" == j.at("app")));
        } catch(const json::out_of_range& oor){ comp_app.chi = comp_app.chi_ff = false; }
        
    }
    catch(const json::out_of_range& oor){
        std::string err = std::string(oor.what()) + " in file '" + file_name + "'";
        throw std::out_of_range(err);
    }
}

void Sim_Param::print_info(std::string out, std::string app) const
{
    if (out == "")
    {
        BOOST_LOG_TRIVIAL(info) << "\n"
        "*********************\n"
        "SIMULATION PARAMETERS\n"
        "*********************\n"
        "Ng:\t\t" << box_opt.Ng << "\n"
        "Num_par:\t" << box_opt.par_num_1d << "^3\n"
        "Num_mesh:\t" << box_opt.mesh_num << "^3\n"
        "Num_mesh_pwr:\t" << box_opt.mesh_num_pwr << "^3\n"
        "Box size:\t" << box_opt.box_size << " Mpc/h\n"
        "Redshift:\t" << integ_opt.z_in << " ---> " << integ_opt.z_out << "\n"
        "Pk:\t\t[sigma_8 = " << cosmo.sigma8 << ", ns = " << cosmo.ns << ", k_smooth = " << sqrt(cosmo.k2_G) << "]\n" << 
        "\t\t[transfer_function_method = " << find_value(transfer_function_method, cosmo.config.transfer_function_method) << "]\n" <<
        "\t\t[matter_power_spectrum_method = " << find_value(matter_power_spectrum_method, cosmo.config.matter_power_spectrum_method) << "]\n" <<
        "\t\t[mass_function_method = " << find_value(mass_function_method, cosmo.config.mass_function_method) << "]\n" <<
        "\t\t[baryons_power_spectrum_method = " << find_value(baryons_power_spectrum_method, cosmo.config.baryons_power_spectrum_method) << "]\n" <<
        "AA:\t\t[nu = " << app_opt.nu_dim << " (Mpc/h)^2]\n"  <<
        "LL:\t\t[rs = " << app_opt.rs << ", a = " << app_opt.a << ", M = " << app_opt.M << ", Hc = " << app_opt.Hc << "]\n" <<
        "CHI:\t\t[beta = " << chi_opt.beta << ", n = " << chi_opt.n << ", phi = " << chi_opt.phi << "\n" <<
        "num_thread:\t << " << run_opt.nt << "\n" <<
         "Output:\t\t'" << out_opt.out_dir << "'\n";
    }
    else
    {
        std::string file_name = out + "sim_param.json";
        std::ofstream o(file_name);

        json j = {
            {"box_opt", box_opt},
            {"integ_opt", integ_opt},
            {"cosmo", cosmo},
            {"app_opt", app_opt},
            {"run_opt", run_opt},
            {"out_opt", out_opt},
            {"k_nyquist", other_par.nyquist},
            {"app", app}
        };
        if (comp_app.chi | comp_app.chi_ff) j["chi_opt"] = chi_opt;
        if (app == "test") j["test_opt"] = test_opt;

        o << std::setw(2) << j << std::endl;
        o.close();
    }
}

void Sim_Param::print_info() const
{
	Sim_Param::print_info("", "");
}

void Sim_Param::reset()
{
    run_opt.reset();
    comp_app.reset();
}

bool Sim_Param::is_ready()
{
    bool is_ready = true;
    is_ready &= run_opt.is_ready();
    is_ready &= comp_app.is_ready();
    return is_ready;
}
