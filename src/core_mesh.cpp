#include "core.h"
#include "core_mesh.h"

#ifndef ORDER
#define ORDER 1
#endif

using namespace std;

void get_k_vec(int N, int index, int* k_vec)
{
	k_vec[0] = index / ((N/2 + 1)*N);
	k_vec[1] = (index / (N/2 + 1)) % N;
	k_vec[2] = index % (N/2 + 1);
	
	for (int i =0; i<2; i++) if (k_vec[i] > N/2) k_vec[i] -= N; // k_vec[2] is ALWAYS less or equal than N/2 (real FFTW)
}

void get_k_vec(int N, int index, Vec_3D<int> &k_vec)
{
	k_vec[0] = index / ((N/2 + 1)*N);
	k_vec[1] = (index / (N/2 + 1)) % N;
	k_vec[2] = index % (N/2 + 1);
	
	for (int i =0; i<2; i++) if (k_vec[i] > N/2) k_vec[i] -= N; // k_vec[2] is ALWAYS less or equal than N/2 (real FFTW)
}

FTYPE get_k_sq(int N, int index)
{
	int k_vec[3];
	FTYPE tmp = 0;
	get_k_vec(N, index, k_vec);
	for (int i =0; i<3; i++) tmp += pow(k_vec[i],2);
	return tmp;
}

template<typename T>
static T get_per(T vec, int per)
{
    return ((vec >= per) || (vec < 0) ) ? vec - per * floor( vec / per ) : vec;
}

static int get_per(int vec, int per)
{
    if ((vec >= per) || (vec < 0) ){
        vec %= per;
        return (vec < 0) ? vec + per : vec;
    }
    else return vec;
}

template<typename T>
void get_per(Vec_3D<T> &position, int per)
{
    for (T& pos : position) pos = get_per(pos, per);
}

void get_per(Vec_3D<int> &position, int perx, int pery, int perz)
{
    position[0] = get_per(position[0], perx);
    position[1] = get_per(position[1], pery);
    position[2] = get_per(position[2], perz);
}

void get_per(Particle_v* particles, const unsigned par_num, const int per)
{
    #pragma omp parallel for
    for (unsigned i = 0; i < par_num; i++)
    {
        get_per(particles[i].position, per);
    }
}

template<typename T>
inline T get_distance_1D(const T x_1, const T x_2, const int per)
{	
	T d = abs(x_2 - x_1);
	if (d <= per / 2) return d; // most probable, first condition
	else if (d <= per) return per - d;
	else d = fmod(d, T(per)); // fmod unlikely to evaluate, speed up code
	if (d <= per / 2) return d;
	else return per - d;
}

template<typename T>
inline T get_distance_1D(const T x_1, const int x_2, const int per){ return get_distance_1D(x_1, (T)x_2, per); }

template<typename T>
inline T get_sgn_distance_1D(T x_from, T x_to, int per)
{	// return signed (oriented) distance, e.g. x_to - x_from (no periodicity)
	// periodicity makes this a little bit trickier
	T d = x_from - x_to;
	if (abs(d) <= per / 2) return d; // most probable, first condition
	else if (abs(d) <= per) return d-sgn(d)*per;
	else d = fmod(d, T(per)); // fmod unlikely to evaluate, speed up code
	if (abs(d) <= per / 2) return d;
	else return d-sgn(d)*per;
}

FTYPE get_distance(const Vec_3D<FTYPE> &x_1, const Vec_3D<FTYPE> &x_2, int per)
{
	FTYPE dst = 0;
	for (int i = 0; i < 3; i++) dst+= pow_(get_distance_1D(x_1[i], x_2[i], per), 2);
	return sqrt(dst);
}

Vec_3D<FTYPE> get_sgn_distance(const Vec_3D<FTYPE> &x_from, const Vec_3D<FTYPE> &x_to, int per)
{
	Vec_3D<FTYPE> dst;
	for (int i = 0; i < 3; i++) dst[i] = get_sgn_distance_1D(x_from[i], x_to[i], per);
	return dst;
}

template<unsigned N> static FTYPE wgh_sch(const Vec_3D<FTYPE> &x, const Vec_3D<int>& y, int mesh_num);
// The weighting scheme used to assign values to the mesh points or vice versa
// Return value of assigment function on mesh point y from particle in x

// template<> FTYPE wgh_sch<0>(const Vec_3D<FTYPE> &x, Vec_3D<int> y, int mesh_num)
// { // NGP: Nearest grid point
//     get_per(y, mesh_num);
//     for (int i = 0; i < 3; i++) if ((int)x[i] != y[i]) return 0;
//     return 1;
// }

template<> FTYPE wgh_sch<1>(const Vec_3D<FTYPE> &x, const Vec_3D<int>& y, int mesh_num)
{ // CIC: Cloud in cells
    FTYPE dx, w = 1;
    for (int i = 0; i < 3; i++)
    {
        dx = get_distance_1D(x[i], y[i], mesh_num);
        if (dx > 1) return 0;
        else w *= 1 - dx;
    }
    return w;
}

template<> FTYPE wgh_sch<2>(const Vec_3D<FTYPE> &x, const Vec_3D<int>& y, int mesh_num)
{ // TSC: Triangular shaped clouds
    FTYPE dx, w = 1;
    for (int i = 0; i < 3; i++)
    {
        dx = get_distance_1D(x[i], y[i], mesh_num);
        if (dx > 1.5) return 0;
        else if (dx > 0.5) w *= pow_(1.5 - dx, 2) / 2;
        else w *= 3 / FTYPE(4) - dx*dx;
    }
    return w;
}

/**
 * @class:	IT
 * @brief:	class for effective iteration of cube of mesch cells
 */

template<int points>
IT<points>::IT(const Vec_3D<FTYPE> &pos): counter(0)
{
    for(unsigned i = 0; i < 3; i++){
        vec[i] = (int)(pos[i] - 0.5*(points - 2));
    }
}

template<int points>
IT<points>::IT(const Vec_3D<FTYPE> &pos, FTYPE Hc): counter(0)
{
    for(unsigned i = 0; i < 3; i++){
        vec[i] = (int)(pos[i]/Hc) - 1;
    }
}

template<int points>
bool IT<points>::iter(){
    if (++counter == (points*points*points)) return false;
    vec[2]++;
    if ((counter % points) == 0){
        vec[2] -= points;
        vec[1]++;
        if ((counter % (points*points)) == 0){
            vec[1] -= points;
            vec[0]++;
        }
    }
    return true;
}

void assign_to(Mesh* field, const Vec_3D<FTYPE> &position, const FTYPE value)
{
    IT<ORDER+1> it(position);
    do{
        #pragma omp atomic
        (*field)(it.vec) += value * wgh_sch<ORDER>(position, it.vec, field->N);
    } while( it.iter() );
}

void assign_to(vector<Mesh>* field, const Vec_3D<FTYPE> &position, const Vec_3D<FTYPE>& value)
{
    IT<ORDER+1> it(position);
    FTYPE w;
    do{
        w = wgh_sch<ORDER>(position, it.vec, (*field)[0].N); //< resuse the same weigh for every field in vector
        for (int i = 0; i < 3; i++)
        {
            #pragma omp atomic
            (*field)[i](it.vec) += value[i] * w;
        }
	} while( it.iter() );
}

void assign_from(const Mesh &field, const Vec_3D<FTYPE> &position, FTYPE* value)
{
	IT<ORDER+1> it(position);
    do{
        #pragma omp atomic
        *value += field(it.vec) * wgh_sch<ORDER>(position, it.vec, field.N);
	} while( it.iter() );
}

void assign_from(const vector<Mesh> &field, const Vec_3D<FTYPE> &position, Vec_3D<FTYPE>* value)
{
    IT<ORDER+1> it(position);
    FTYPE w;
    do{
        w = wgh_sch<ORDER>(position, it.vec, field[0].N); //< resuse the same weigh for every field in vector
        for (int i = 0; i < 3; i++)
        {
            #pragma omp atomic
            (*value)[i] += field[i](it.vec) * w;
        }
	} while( it.iter() );
}

void fftw_execute_dft_r2c(const FFTW_PLAN_TYPE &p_F, Mesh& rho)
{
	FFTW_EXEC_R2C(p_F, rho.real(), rho.complex());
	rho /= pow(rho.N, 3); //< normalization
}

void fftw_execute_dft_c2r(const FFTW_PLAN_TYPE &p_B, Mesh& rho)
{
	FFTW_EXEC_C2R(p_B, rho.complex(), rho.real());
}

void fftw_execute_dft_r2c_triple(const FFTW_PLAN_TYPE &p_F, vector<Mesh>& rho)
{
	for (int i = 0; i < 3; i++) fftw_execute_dft_r2c(p_F, rho[i]);
}

void fftw_execute_dft_c2r_triple(const FFTW_PLAN_TYPE &p_B, vector<Mesh>& rho)
{
	for (int i = 0; i < 3; i++) fftw_execute_dft_c2r(p_B, rho[i]);
}

FTYPE mean(FTYPE* p_data, int len)
{
	FTYPE tmp = 0;
	
	#pragma omp parallel for reduction(+:tmp)
	for (int i = 0; i < len; i++) tmp += p_data[i];
	
	return tmp / len;
}

FTYPE std_dev(FTYPE* p_data, int len, FTYPE t_mean)
{
	FTYPE tmp = 0;
	
	#pragma omp parallel for reduction(+:tmp)
	for (int i = 0; i < len; i++) tmp += (p_data[i]-t_mean)*(p_data[i]-t_mean);
	
	return sqrt(tmp / len);
}

FTYPE min(FTYPE* p_data, int len)
{
    return *std::min_element(p_data,p_data+len);
}

FTYPE max(FTYPE* p_data, int len)
{
    return *std::max_element(p_data,p_data+len);
}

FTYPE min(const vector<FTYPE>& data)
{
    return *std::min_element(data.begin(), data.end());
}

FTYPE max(const vector<FTYPE>& data)
{
    return *std::max_element(data.begin(), data.end());
}

template void get_per(Vec_3D<int> &position, int per);
template void get_per(Vec_3D<FTYPE> &position, int per);
template class IT<ORDER+1>;
template class IT<3>;

#ifdef TEST
#include "test_core_mesh.cpp"
#endif