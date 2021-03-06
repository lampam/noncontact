#pragma once

#include <vector>
#include <chrono>
#include <cmath>

#include <Eigen/Dense>

#include "lattice.hpp"

// FIXME: `using` in header = badness
using namespace Eigen;

template <class T>
class LJPseudoPotential {
	public:

		LJPseudoPotential(std::size_t d1, std::size_t d2)
		: z0(d1, d2)
		, coeff6(d1, d2)
		, coeff12(d1, d2)
		{ }


		#ifdef PP_LINEAR
		// Original version, which is far simpler (a linear regression).
		// It's here so I can compare output, and should be removed in the future.
		static LJPseudoPotential fit_to_data(Lattice3<T> potential)
		{
			LJPseudoPotential result {potential.axis_size(0), potential.axis_size(1)};

			// make coeff lattices in the shape of the x & y dimensions of potential
			result.z0      = make_sub_lattice<T>(potential, 0, 1);
			result.coeff6  = make_sub_lattice<T>(potential, 0, 1);
			result.coeff12 = make_sub_lattice<T>(potential, 0, 1);

			// Collect z-coordinates of data set
			Array<T, Dynamic, 1> zarr {potential.size_2(), 1};
			for (std::size_t k=0; k < potential.size_2(); k++) {
				zarr(k) = potential.coord(2,k);
			}

			// Generate coefficient matrix
			Matrix<T, Dynamic, 2> xmat {potential.size_2(), 2};
			xmat.col(0) = zarr.pow(-6);
			xmat.col(1) = zarr.pow(-12);

			// Generate least squares system solver
			auto solver = xmat.colPivHouseholderQr();

			// Solve for a best fit along z at each (x,y)
			for (std::size_t i=0; i < potential.size_0(); i++) {
				for (std::size_t j=0; j < potential.size_1(); j++) {

					// Collect potential data at (x,y)
					Matrix<T, Dynamic, 1> pvec {potential.size_2(), 1};
					for (std::size_t k=0; k < potential.size_2(); k++) {
						pvec(k) = potential(i,j,k);
					}

					// FIXME: a third parameter is needed (the z=0 point)

					// Solve the least squares system
					Matrix<T, 2, 1> soln = solver.solve(pvec);

					// Record
					result.z0(i,j)      = 0; // FIXME compute this proper
					result.coeff6(i,j)  = soln(0,0);
					result.coeff12(i,j) = soln(1,0); 
				}
			}

			return result;
		}

		#else // #ifdef PP_LINEAR

		static LJPseudoPotential fit_to_data(Lattice3<T> potential, T tolerance=1E-9)
		{
			LJPseudoPotential result {potential.size_0(), potential.size_1()};

			// make coeff lattices in the shape of the x & y dimensions of potential
			result.z0      = make_sub_lattice<T>(potential, 0, 1);
			result.coeff6  = make_sub_lattice<T>(potential, 0, 1);
			result.coeff12 = make_sub_lattice<T>(potential, 0, 1);

			// Declare our main matrices & reserve memory
			Array<T,  Dynamic, 1> zarr     {potential.size_2(), 1};
			Array<T,  Dynamic, 1> varr     {potential.size_2(), 1};
			Matrix<T, Dynamic, 1> errors   {potential.size_2(), 1};
			Matrix<T, Dynamic, 3> jacobian {potential.size_2(), 3};

			// Collect z data (our independent variable)
			for (std::size_t k=0; k < potential.size_2(); k++)
				zarr(k) = potential.coord_2(k);

			// Square sum error at each step
			T prev_sqsum = std::numeric_limits<T>::max();
			T this_sqsum = std::numeric_limits<T>::max();

			// Solve for a best fit along z at each (x,y)
			for (std::size_t i=0; i < potential.size_0(); i++) {
				for (std::size_t j=0; j < potential.size_1(); j++) {

					// Collect potential data (our dependent variable) at (x,y)
					for (std::size_t k=0; k < potential.size_2(); k++)
						varr(k) = potential(i,j,k);

					// Form initial guess, and unpack it into more meaningfully named vars
					Matrix<T, 3, 1> guess;
					T & guess_z0  = guess(0,0);
					T & guess_c6  = guess(1,0);
					T & guess_c12 = guess(2,0);

					// TODO fixed guess = nnnnooooooope
					guess_z0  = 0.05;
					guess_c6  = -1.;
					guess_c12 = +1.;

					while (true) {
						// z - z0
						Array<T, Dynamic, 1> delta_z = zarr - guess_z0;

						// Differences between potential data and potential computed from guess
						errors = varr - guess_c6 * delta_z.pow(-6) - guess_c12 * delta_z.pow(-12);


						// TODO: I think there needs to be more freedom with the stop condition.
						//       Or *less* freedom. >_>

						// Stop condition:  Check total error
						prev_sqsum = this_sqsum;
						this_sqsum = errors.squaredNorm();

						if (!std::isnormal(prev_sqsum)) {
							assert(!std::isinf(prev_sqsum) && !std::isnan(prev_sqsum));
							break;	// our answer is perfect. Bail out before we divide by zero >_>
						}

						if (fabs(this_sqsum - prev_sqsum) < tolerance)
							break;  // Natural stopping condition (error has converged)


						// Derivatives of the potential with respect to each fit parameter
						jacobian.col(0) = 6. * guess_c6 * delta_z.pow(-7) + 12. * guess_c12 * delta_z.pow(-13);
						jacobian.col(1) = delta_z.pow(-6);
						jacobian.col(2) = delta_z.pow(-12);

						// Refine guess by solving the normal equation
						guess += jacobian.colPivHouseholderQr().solve(errors);
					}

					// Record
					result.z0(i,j)      = guess_z0;
					result.coeff6(i,j)  = guess_c6;
					result.coeff12(i,j) = guess_c12;
				}
			}

			return result;
		}
		#endif


		T value_at(T x, T y, T z) {
			// do nearest neighbor for x and y <---- HORRIBLE IDEA  FIXME FIXME FIXME
			int i = nearest_index_x(x);
			int j = nearest_index_y(y);
			T r   = z - z0(i,j);
			T c6  = coeff6(i,j);
			T c12 = coeff12(i,j);

			return c6 * pow(r,-6) + c12 * pow(r,-12);
		}

		T force_z_derivative_at(T x, T y, T z) {
			// do nearest neighbor for x and y <---- HORRIBLE IDEA  FIXME FIXME FIXME
			int i = nearest_index_x(x);
			int j = nearest_index_y(y);
			T r   = z - z0(i,j);
			T c6  = coeff6(i,j);
			T c12 = coeff12(i,j);

			// expression obtained by le maths
			T result = 0;
			result += (36*4*26*27) * pow(r, -28) * c12 * c12;
			result += (36*4*20*21) * pow(r, -22) * c12 * c6;
			result += (36*1*14*15) * pow(r, -15) * c6  * c6;

			// TODO: This final expression should multiplied by some scalar constant related to
			//       polarization of the tip. Not sure what determines this constant, not sure
			//       where the best place is to account for it, not sure whether or not is is
			//       important enough to warrant further muddying the responsibilities of this class >_>
			return result;
		}

	private:
		// returns lattice indices of nearest point on lattice
		int nearest_index_x (T x) {
			return int((x - coeff6.lower_coord_0())/coeff6.coord_step_0() + 0.5);
		}
		int nearest_index_y (T y) {
			return int((y - coeff6.lower_coord_1())/coeff6.coord_step_1() + 0.5);
		}

		Lattice2<T> z0;
		Lattice2<T> coeff6;
		Lattice2<T> coeff12;
};

