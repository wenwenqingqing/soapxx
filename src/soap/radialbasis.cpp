#include <math.h>
#include <boost/math/special_functions/erf.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/io.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/vector.hpp>

#include "soap/base/exceptions.hpp"
#include "soap/linalg/operations.hpp"
#include "soap/globals.hpp"
#include "soap/radialbasis.hpp"

namespace soap {

namespace ub = boost::numeric::ublas;

// ======================
// RadialBasis BASE CLASS
// ======================

void RadialBasis::configure(Options &options) {
    _N = options.get<int>("radialbasis.N");
    _Rc = options.get<double>("radialcutoff.Rc"); // <- !! Modified below for adaptive basis sets !!
    _integration_steps = options.get<int>("radialbasis.integration_steps");
    _mode = options.get<std::string>("radialbasis.mode");
}

void RadialBasis::computeCoefficients(
        vec d,
        double r,
        double particle_sigma,
        radcoeff_t &Gnl,
        radcoeff_t *dGnl_dx,
        radcoeff_t *dGnl_dy,
        radcoeff_t *dGnl_dz) {
	throw soap::base::NotImplemented("RadialBasis::computeCoefficients");
	return;
}

// ======================
// RadialBasisGaussian
// ======================

RadialBasisGaussian::RadialBasisGaussian() {
	 _type = "gaussian";
	 _is_ortho = false;
	 _sigma = 0.5;
}

void RadialBasisGaussian::clear() {
	for (basis_it_t bit=_basis.begin(); bit!=_basis.end(); ++bit) {
		delete *bit;
	}
	_basis.clear();
}

void RadialBasisGaussian::configure(Options &options) {
	RadialBasis::configure(options);
    _sigma = options.get<double>("radialbasis.sigma");

    // CREATE & STORE EQUISPACED RADIAL GAUSSIANS
    this->clear();

    if (_mode == "equispaced") {
		double dr = _Rc/(_N-1);
		for (int i = 0; i < _N; ++i) {
			double r = i*dr;
			double sigma = _sigma;
			basis_fct_t *new_fct = new basis_fct_t(r, sigma);
			_basis.push_back(new_fct);
		}
    }
    else if (_mode == "adaptive") {
        //double delta = 0.5;
        int L = options.get<int>("angularbasis.L");
        double r = 0.;
        double sigma = 0.;
        double sigma_0 = _sigma;
        double sigma_stride_factor = 0.5;
//        while (r < _Rc) {
//            sigma = sqrt(4./(2*L+1))*(r+delta);
//            basis_fct_t *new_fct = new basis_fct_t(r, sigma);
//			_basis.push_back(new_fct);
//			r = r + sigma;
//        }
        for (int i = 0; i < _N; ++i) {
        	//sigma = sqrt(4./(2*L+1))*(r+delta);
        	sigma = sqrt(4./(2*L+1)*r*r + sigma_0*sigma_0);
        	//std::cout << r << " " << sigma << std::endl;
        	r = r + sigma_stride_factor*sigma;
        }
        double scale = 1.; //_Rc/(r-sigma);
        r = 0;
        for (int i = 0; i < _N; ++i) {
        	sigma = sqrt(4./(2*L+1)*r*r + sigma_0*sigma_0);
        	basis_fct_t *new_fct = new basis_fct_t(scale*r, sigma);
			_basis.push_back(new_fct);
			//std::cout << r << " " << sigma << std::endl;
			r = r+sigma_stride_factor*sigma;
        }
        _Rc = r - sigma_stride_factor*sigma;
        options.set("radialcutoff.Rc", _Rc);
        GLOG() << "Adjusted radial cutoff to " << _Rc
        	<< " based on sigma_0 = " << sigma_0 << ", L = " << L << ", stride = " << sigma_stride_factor << std::endl;
    }
    else {
    	throw std::runtime_error("Not implemented.");
    }
    // SUMMARIZE
    GLOG() << "Created " << _N << " radial Gaussians at r = { ";
    for (basis_it_t bit = _basis.begin(); bit != _basis.end(); ++bit) {
        GLOG() << (*bit)->_r0 << " (" << (*bit)->_sigma << ") ";
    }
    GLOG() << "}" << std::endl;

    // COMPUTE OVERLAP MATRIX s_{ij} = \int g_i(r) g_j(r) r^2 dr
    _Sij = ub::matrix<double>(_N, _N);
    basis_it_t it;
    basis_it_t jt;
    int i;
    int j;
    for (it = _basis.begin(), i = 0; it != _basis.end(); ++it, ++i) {
    	for (jt = _basis.begin(), j = 0; jt != _basis.end(); ++jt, ++j) {
            double a = (*it)->_alpha;
            double b = (*jt)->_alpha;
            double r0 = (*it)->_r0;
            double r1 = (*jt)->_r0;
            double w = a + b;
            double W0 = a*r0 + b*r1;
            double s =
				1./(4.*pow(w, 2.5))*exp(-a*r0*r0 -b*r1*r1)*(
					2*sqrt(w)*W0 +
					sqrt(M_PI)*exp(W0*W0/w)*(w+2*W0*W0)*(
						1 - boost::math::erf<double>(-W0/sqrt(w))
					)
				);
			s *= (*it)->_norm_r2_g2_dr*(*jt)->_norm_r2_g2_dr;
			_Sij(i,j) = s;
    	}
    }
    // REPORT
	GLOG() << "Radial basis overlap matrix" << std::endl;
	for (it = _basis.begin(), i = 0; it != _basis.end(); ++it, ++i) {
		for (jt = _basis.begin(), j = 0; jt != _basis.end(); ++jt, ++j) {
			 GLOG() << boost::format("%1$+1.4e") % _Sij(i,j) << " " << std::flush;
		}
		GLOG() << std::endl;
	}

    // ORTHONORMALIZATION VIA CHOLESKY DECOMPOSITION
    _Uij = _Sij;
    soap::linalg::linalg_cholesky_decompose(_Uij);
    // REPORT
    GLOG() << "Radial basis Cholesky decomposition" << std::endl;
	for (it = _basis.begin(), i = 0; it != _basis.end(); ++it, ++i) {
		for (jt = _basis.begin(), j = 0; jt != _basis.end(); ++jt, ++j) {
			 GLOG() << boost::format("%1$+1.4e") % _Uij(i,j) << " " << std::flush;
		}
		GLOG() << std::endl;
	}

    // ZERO UPPER DIAGONAL OF U
    for (it = _basis.begin(), i = 0; it != _basis.end(); ++it, ++i)
		for (jt = it+1, j = i+1; jt != _basis.end(); ++jt, ++j)
			 _Uij(i,j) = 0.0;
    _Tij = _Uij;
    soap::linalg::linalg_invert(_Uij, _Tij);
    // REPORT
    GLOG() << "Radial basis transformation matrix" << std::endl;
	for (it = _basis.begin(), i = 0; it != _basis.end(); ++it, ++i) {
		for (jt = _basis.begin(), j = 0; jt != _basis.end(); ++jt, ++j) {
			 GLOG() << boost::format("%1$+1.4e") % _Tij(i,j) << " " << std::flush;
		}
		GLOG() << std::endl;
	}
}

void RadialBasisGaussian::computeCoefficients(
        vec d,
        double r,
        double particle_sigma,
        radcoeff_t &Gnl,
        radcoeff_t *dGnl_dx,
        radcoeff_t *dGnl_dy,
        radcoeff_t *dGnl_dz) {

    bool gradients = false;
    if (dGnl_dx) {
        assert(dGnl_dy != NULL && dGnl_dz != NULL);
        gradients = true;
    }

	// Delta-type expansion =>
	// Second (l) dimension of <save_here> and <particle_sigma> ignored here
	if (particle_sigma < RadialBasis::RADZERO) {
		basis_it_t it;
		int n = 0;
		for (it = _basis.begin(), n = 0; it != _basis.end(); ++it, ++n) {
			double gn_at_r = (*it)->at(r);
			for (int l = 0; l != Gnl.size2(); ++l) {
			    Gnl(n, l) = gn_at_r;
			}
		}
		Gnl = ub::prod(_Tij, Gnl);
	}
	else {

		// Particle properties
		double ai = 1./(2*particle_sigma*particle_sigma);
		double ri = r;
		SphericalGaussian gi_sph(vec(0,0,0), particle_sigma); // <- position should not matter, as only normalization used here
		double norm_g_dV_sph_i = gi_sph._norm_g_dV;

		int k = 0;
		basis_it_t it;
		for (it = _basis.begin(), k = 0; it != _basis.end(); ++it, ++k) {
			// Radial Gaussian properties
			double ak = (*it)->_alpha;
			double rk = (*it)->_r0;
			double norm_r2_g2_dr_rad_k = (*it)->_norm_r2_g2_dr;
			// Combined properties
			double beta_ik = ai+ak;
			double rho_ik = ak*rk/beta_ik;

			double sigma_ik = sqrt(0.5/beta_ik);
			RadialGaussian gik_rad(rho_ik, sigma_ik);
			double norm_r2_g_dr_rad_ik = gik_rad._norm_r2_g_dr;

			double prefac =
			    4*M_PI *
				norm_r2_g2_dr_rad_k*norm_g_dV_sph_i/norm_r2_g_dr_rad_ik *
				exp(-ai*ri*ri) *
				exp(-ak*rk*rk*(1-ak/beta_ik));

			// NUMERICAL INTEGRATION
			// ZERO COEFFS
			for (int l = 0; l != Gnl.size2(); ++l) {
			    Gnl(k, l) = 0.0;
			}

			// SIMPSON'S RULE
			double r_min = rho_ik - 4*sigma_ik;
			double r_max = rho_ik + 4*sigma_ik;
			if (r_min < 0.) {
				r_max -= r_min;
				r_min = 0.;
			}
			int n_steps = this->_integration_steps;
			double delta_r_step = (r_max-r_min)/(n_steps-1);
			int n_sample = 2*n_steps+1;
			double delta_r_sample = 0.5*delta_r_step;

			// Compute samples for all l's
			// For each l, store integrand at r_sample
			ub::matrix<double> integrand_l_at_r = ub::zero_matrix<double>(Gnl.size2(), n_sample);
			// TODO ub::matrix<double> grad_integrand_l_at_r = ...
			// TODO Apply prefactor later (after integration)
			// TODO gamma_kl = prefactor*integral_kl
			// TODO grad_gamma_kl = -2*ai*ri*gamma_kl + prefactor*grad_integral_kl
			for (int s = 0; s < n_sample; ++s) {
				// f0 f1 f2 f3 ....  f-3 f-2 f-1
				// |-----||----||----||-------|
				double r_sample = r_min - delta_r_sample + s*delta_r_sample;
				// ... Generate Bessels
				std::vector<double> sph_il = ModifiedSphericalBessel1stKind::eval(Gnl.size2(), 2*ai*ri*r_sample);
				// ... Compute & store integrands
				for (int l = 0; l != Gnl.size2(); ++l) {
					integrand_l_at_r(l, s) =
					    prefac*
						r_sample*r_sample*
						sph_il[l]*
						exp(-beta_ik*(r_sample-rho_ik)*(r_sample-rho_ik))*norm_r2_g_dr_rad_ik;
				}
			}
			// Apply Simpson's rule
			for (int s = 0; s < n_steps; ++s) {
				for (int l = 0; l != Gnl.size2(); ++l) {
				    Gnl(k,l) +=
						delta_r_step/6.*(
							integrand_l_at_r(l, 2*s)+
							4*integrand_l_at_r(l, 2*s+1)+
							integrand_l_at_r(l, 2*s+2)
						);
				}
			}

		}
		Gnl = ub::prod(_Tij, Gnl);
	}
    return;
}

// ======================
// RadialBasisFactory
// ======================

void RadialBasisFactory::registerAll(void) {
	RadialBasisOutlet().Register<RadialBasisGaussian>("gaussian");
	RadialBasisOutlet().Register<RadialBasisLegendre>("legendre");
}

}

BOOST_CLASS_EXPORT_IMPLEMENT(soap::RadialBasisGaussian);
