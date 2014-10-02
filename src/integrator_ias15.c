/**
 * @file 	integrator.c
 * @brief 	IAS15 integrator.
 * @author 	Hanno Rein <hanno@hanno-rein.de>
 * @detail	This file implements the IAS15 integration scheme.  
 * IAS stands for Integrator with Adaptive Step-size control, 15th 
 * order. This scheme is a fifteenth order integrator well suited for 
 * high accuracy orbit integration with non-conservative forces.
 * For more details see Rein & Spiegel 2014. Also see Everhart, 1985,
 * ASSL Vol. 115, IAU Colloq. 83, Dynamics of Comets, Their Origin 
 * and Evolution, 185 for the original implementation by Everhart.
 * Part of this code is based a function from the ORSE package.
 * See orsa.sourceforge.net for more details on their implementation.
 *
 * 
 * @section 	LICENSE
 * Copyright (c) 2011-2012 Hanno Rein, Dave Spiegel.
 * Copyright (c) 2002-2004 Pasquale Tricarico.
 *
 * This file is part of rebound.
 *
 * rebound is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * rebound is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <string.h>
// Uncomment the following line to generate numerical constants with extended precision.
//#define GENERATE_CONSTANTS
#ifdef GENERATE_CONSTANTS
#include <gmp.h>
void integrator_generate_constants();
#endif // GENERATE_CONSTANTS
#include "particle.h"
#include "main.h"
#include "gravity.h"
#include "boundaries.h"
#include "problem.h"
#include "output.h"
#include "tools.h"

#ifdef TREE
#error IAS15 integrator not working with TREE module.
#endif
#ifdef MPI
#error IAS15 integrator not working with MPI.
#endif

int 	integrator_force_is_velocitydependent	= 1;	// Turn this off to safe some time if the force is not velocity dependent.
double 	integrator_epsilon 			= 1e-5;	// Precision parameter 
							// If it is zero, then a constant timestep is used. 
							// if 0: estimate the fractional error by max(acceleration_error/acceleration).
double 	integrator_min_dt 			= 0;	// Minimum timestep used as a floor when adaptive timestepping is enabled.
double 	integrator_max_dt 			= 0;
unsigned long integrator_iterations_max_exceeded= 0;	// Count how many times the iteration did not converge
const double safety_factor 			= 0.25;	// Maximum increase/deacrease of consecutve timesteps.


// Gauss Radau spacings
const double h[9]	= { 0.0, 0.0562625605369221464656521910, 0.1802406917368923649875799428, 0.3526247171131696373739077702, 0.5471536263305553830014485577, 0.7342101772154105410531523211, 0.8853209468390957680903597629, 0.9775206135612875018911745004,1.0}; 
// Other constants
const double r[28] = {0.0562625605369221464656522, 0.1802406917368923649875799, 0.1239781311999702185219278, 0.3526247171131696373739078, 0.2963621565762474909082556, 0.1723840253762772723863278, 0.5471536263305553830014486, 0.4908910657936332365357964, 0.3669129345936630180138686, 0.1945289092173857456275408, 0.7342101772154105410531523, 0.6779476166784883945875001, 0.5539694854785181760655724, 0.3815854601022409036792446, 0.1870565508848551580517038, 0.8853209468390957680903598, 0.8290583863021736216247076, 0.7050802551022034031027798, 0.5326962297259261307164520, 0.3381673205085403850889112, 0.1511107696236852270372074, 0.9775206135612875018911745, 0.9212580530243653554255223, 0.7972799218243951369035946, 0.6248958964481178645172667, 0.4303669872307321188897259, 0.2433104363458769608380222, 0.0921996667221917338008147};
const double c[21] = {-0.0562625605369221464656522, 0.0101408028300636299864818, -0.2365032522738145114532321, -0.0035758977292516175949345, 0.0935376952594620658957485, -0.5891279693869841488271399, 0.0019565654099472210769006, -0.0547553868890686864408084, 0.4158812000823068616886219, -1.1362815957175395318285885, -0.0014365302363708915610919, 0.0421585277212687082291130, -0.3600995965020568162530901, 1.2501507118406910366792415, -1.8704917729329500728817408, 0.0012717903090268677658020, -0.0387603579159067708505249, 0.3609622434528459872559689, -1.4668842084004269779203515, 2.9061362593084293206895457, -2.7558127197720458409721005};
const double d[21] = {0.0562625605369221464656522, 0.0031654757181708292499905, 0.2365032522738145114532321, 0.0001780977692217433881125, 0.0457929855060279188954539, 0.5891279693869841488271399, 0.0000100202365223291272096, 0.0084318571535257015445000, 0.2535340690545692665214616, 1.1362815957175395318285885, 0.0000005637641639318207610, 0.0015297840025004658189490, 0.0978342365324440053653648, 0.8752546646840910912297246, 1.8704917729329500728817408, 0.0000000317188154017613665, 0.0002762930909826476593130, 0.0360285539837364596003871, 0.5767330002770787313544596, 2.2485887607691598182153473, 2.7558127197720458409721005};

// The following values will be set dynamically.
double s[9];				// Summation coefficients 

int N3allocated	= 0; 			// Size of allocated arrays.

double* at   	= NULL;			// Temporary buffer for acceleration
double* x0  	= NULL;			// Temporary buffer for position (used for initial values at h=0) 
double* v0  	= NULL;			//                      velocity
double* a0  	= NULL;			//                      acceleration
double* csx  	= NULL;			//                      compensated summation
double* csv  	= NULL;			//                      compensated summation

double* g[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL} ;
double* b[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL} ;
double* e[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL} ;

// The following values are used for resetting the b and e coefficients if a timestep gets rejected
double* br[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL} ;
double* er[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL} ;
// Helper functions for resetting the b and e coefficients

// Do nothing here. This is only used in a leapfrog-like DKD integrator. IAS15 performs one complete timestep.
void integrator_part1(){
}

// This function updates the acceleration on all particles. 
// It uses the current position and velocity data in the (struct particle*) particles structure.
// Note: this does currently not work with MPI or any TREE module.
void integrator_update_acceleration(){
	PROFILING_STOP(PROFILING_CAT_INTEGRATOR)
	PROFILING_START()
	gravity_calculate_acceleration();
	if (problem_additional_forces) problem_additional_forces();
	PROFILING_STOP(PROFILING_CAT_GRAVITY)
	PROFILING_START()
}

int integrator_ias15_step(); // Does the actual timestep.

void integrator_part2(){
#ifdef GENERATE_CONSTANTS
	integrator_generate_constants();
#endif  // GENERATE_CONSTANTS
	// Try until a step was successful.
	while(!integrator_ias15_step());
}

int dtexp = 0;
int dtexp_substep[64] = {0};
int dtexp_min = 0;

int integrator_ias15_step() {
	const int N3 = 3*N;
	if (N3 > N3allocated) {
		printf("\n");
		for (int l=0;l<7;++l) {
			g[l] = realloc(g[l],sizeof(double)*N3);
			b[l] = realloc(b[l],sizeof(double)*N3);
			e[l] = realloc(e[l],sizeof(double)*N3);
			br[l] = realloc(br[l],sizeof(double)*N3);
			er[l] = realloc(er[l],sizeof(double)*N3);
			for (int k=0;k<N3;k++){
				b[l][k] = 0;
				e[l][k] = 0;
				br[l][k] = 0;
				er[l][k] = 0;
			}
		}
		at = realloc(at,sizeof(double)*N3);
		x0 = realloc(x0,sizeof(double)*N3);
		v0 = realloc(v0,sizeof(double)*N3);
		a0 = realloc(a0,sizeof(double)*N3);
		csx= realloc(csx,sizeof(double)*N3);
		csv= realloc(csv,sizeof(double)*N3);
		for (int i=0;i<N3;i++){
			// Kill compensated summation coefficients
			csx[i] = 0;
			csv[i] = 0;
		}
		N3allocated = N3;
	}
	
	// integrator_update_acceleration(); // Not needed. Forces are already calculated in main routine.
		
	dt = integrator_max_dt;
	for(int i=0;i<-dtexp;i++){
		int st = dtexp_substep[i];
		double st_dt = h[st+1]-h[st];
		dt  *= st_dt;
	}

	printf("mindtxexp %d   %d  %e %d \n",dtexp_min,dtexp,dt, dtexp_substep[-dtexp]);
	// Predict new B values to use at the start of the next sequence. The predicted
	// values from the last call are saved as E. The correction, BD, between the
	// actual and predicted values of B is applied in advance as a correction.
	
	for(int k=0;k<N3;++k) {
		if (isnormal(particles[k/3].dtdone)){
#warning TODO
			const double q1 = dt/particles[k/3].dtdone;
			const double q2 = q1 * q1;
			const double q3 = q1 * q2;
			const double q4 = q2 * q2;
			const double q5 = q2 * q3;
			const double q6 = q3 * q3;
			const double q7 = q3 * q4;

			double be0 = br[0][k];// - er[0][k];
			double be1 = br[1][k];// - er[1][k];
			double be2 = br[2][k];// - er[2][k];
			double be3 = br[3][k];// - er[3][k];
			double be4 = br[4][k];// - er[4][k];
			double be5 = br[5][k];// - er[5][k];
			double be6 = br[6][k];// - er[6][k];


			e[0][k] = q1*(br[6][k]* 7.0 + br[5][k]* 6.0 + br[4][k]* 5.0 + br[3][k]* 4.0 + br[2][k]* 3.0 + br[1][k]*2.0 + br[0][k]);
			e[1][k] = q2*(br[6][k]*21.0 + br[5][k]*15.0 + br[4][k]*10.0 + br[3][k]* 6.0 + br[2][k]* 3.0 + br[1][k]);
			e[2][k] = q3*(br[6][k]*35.0 + br[5][k]*20.0 + br[4][k]*10.0 + br[3][k]* 4.0 + br[2][k]);
			e[3][k] = q4*(br[6][k]*35.0 + br[5][k]*15.0 + br[4][k]* 5.0 + br[3][k]);
			e[4][k] = q5*(br[6][k]*21.0 + br[5][k]* 6.0 + br[4][k]);
			e[5][k] = q6*(br[6][k]* 7.0 + br[5][k]);
			e[6][k] = q7* br[6][k];
			

			b[0][k] = 0.;//e[0][k] + be0;
			b[1][k] = 0.;//e[1][k] + be1;
			b[2][k] = 0.;//e[2][k] + be2;
			b[3][k] = 0.;//e[3][k] + be3;
			b[4][k] = 0.;//e[4][k] + be4;
			b[5][k] = 0.;//e[5][k] + be5;
			b[6][k] = 0.;//e[6][k] + be6;
		}else{
			e[0][k] = 0.; 
			e[1][k] = 0.; 
			e[2][k] = 0.; 
			e[3][k] = 0.; 
			e[4][k] = 0.; 
			e[5][k] = 0.; 
			e[6][k] = 0.; 
			

			b[0][k] = 0.; 
			b[1][k] = 0.; 
			b[2][k] = 0.; 
			b[3][k] = 0.; 
			b[4][k] = 0.; 
			b[5][k] = 0.; 
			b[6][k] = 0.; 
		}
	}


	for(int k=0;k<N;k++) {
		x0[3*k]   = particles[k].x;
		x0[3*k+1] = particles[k].y;
		x0[3*k+2] = particles[k].z;
		v0[3*k]   = particles[k].vx;
		v0[3*k+1] = particles[k].vy;
		v0[3*k+2] = particles[k].vz;
		a0[3*k]   = particles[k].ax;
		a0[3*k+1] = particles[k].ay;  
		a0[3*k+2] = particles[k].az;
	}

	for(int k=0;k<N3;k++) {
		g[0][k] = b[6][k]*d[15] + b[5][k]*d[10] + b[4][k]*d[6] + b[3][k]*d[3]  + b[2][k]*d[1]  + b[1][k]*d[0]  + b[0][k];
		g[1][k] = b[6][k]*d[16] + b[5][k]*d[11] + b[4][k]*d[7] + b[3][k]*d[4]  + b[2][k]*d[2]  + b[1][k];
		g[2][k] = b[6][k]*d[17] + b[5][k]*d[12] + b[4][k]*d[8] + b[3][k]*d[5]  + b[2][k];
		g[3][k] = b[6][k]*d[18] + b[5][k]*d[13] + b[4][k]*d[9] + b[3][k];
		g[4][k] = b[6][k]*d[19] + b[5][k]*d[14] + b[4][k];
		g[5][k] = b[6][k]*d[20] + b[5][k];
		g[6][k] = b[6][k];
	}

	double predictor_corrector_error = 1e300;
	double predictor_corrector_error_last = 2;
	int iterations = 0;	
	// Predictor corrector loop
	// Stops if one of the following conditions is satisfied: 
	//   1) predictor_corrector_error better than 1e-16 
	//   2) predictor_corrector_error starts to oscillate
	//   3) more than 12 iterations
	while(1){
		if(predictor_corrector_error<1e-16){
			break;
		}
		if(iterations > 2 && predictor_corrector_error_last <= predictor_corrector_error){
			break;
		}
		if (iterations>=12){
			integrator_iterations_max_exceeded++;
			const int integrator_iterations_warning = 10;
			if (integrator_iterations_max_exceeded==integrator_iterations_warning ){
				fprintf(stderr,"\n\033[1mWarning!\033[0m At least %d predictor corrector loops in integrator_ias15.c did not converge. This is typically an indication of the timestep being too large.\n",integrator_iterations_warning);
			}
			break;								// Quit predictor corrector loop
		}
		predictor_corrector_error_last = predictor_corrector_error;
		predictor_corrector_error = 0;
		iterations++;

		for(int n=1;n<8;n++) {							// Loop over interval using Gauss-Radau spacings

			// Prepare particles arrays for force calculation
			for(int i=0;i<N;i++) {						// Predict positions at interval n using b values
				if (particles[i].dtexp < dtexp){
					particles[i].x = particles[i].xpast[-dtexp+1][n];
					particles[i].y = particles[i].ypast[-dtexp+1][n];
					particles[i].z = particles[i].zpast[-dtexp+1][n];
				}else{
					const double hn = h[n]+(t-particles[i].tdone)/dt;
					s[0] = dt * hn;
					s[1] = s[0] * s[0] / 2.;
					s[2] = s[1] * hn / 3.;
					s[3] = s[2] * hn / 2.;
					s[4] = 3. * s[3] * hn / 5.;
					s[5] = 2. * s[4] * hn / 3.;
					s[6] = 5. * s[5] * hn / 7.;
					s[7] = 3. * s[6] * hn / 4.;
					s[8] = 7. * s[7] * hn / 9.;

					const int k0 = 3*i+0;
					const int k1 = 3*i+1;
					const int k2 = 3*i+2;

					double xk0  = csx[k0] + (s[8]*b[6][k0] + s[7]*b[5][k0] + s[6]*b[4][k0] + s[5]*b[3][k0] + s[4]*b[2][k0] + s[3]*b[1][k0] + s[2]*b[0][k0] + s[1]*a0[k0] + s[0]*v0[k0] );
					particles[i].x = xk0 + x0[k0];
					double xk1  = csx[k1] + (s[8]*b[6][k1] + s[7]*b[5][k1] + s[6]*b[4][k1] + s[5]*b[3][k1] + s[4]*b[2][k1] + s[3]*b[1][k1] + s[2]*b[0][k1] + s[1]*a0[k1] + s[0]*v0[k1] );
					particles[i].y = xk1 + x0[k1];
					double xk2  = csx[k2] + (s[8]*b[6][k2] + s[7]*b[5][k2] + s[6]*b[4][k2] + s[5]*b[3][k2] + s[4]*b[2][k2] + s[3]*b[1][k2] + s[2]*b[0][k2] + s[1]*a0[k2] + s[0]*v0[k2] );
					particles[i].z = xk2 + x0[k2];
				}
			}
			
			if (problem_additional_forces && integrator_force_is_velocitydependent){
				printf("Error! Velocity dependent forced not implemented yet\n");
				s[0] = dt * h[n];
				s[1] =      s[0] * h[n] / 2.;
				s[2] = 2. * s[1] * h[n] / 3.;
				s[3] = 3. * s[2] * h[n] / 4.;
				s[4] = 4. * s[3] * h[n] / 5.;
				s[5] = 5. * s[4] * h[n] / 6.;
				s[6] = 6. * s[5] * h[n] / 7.;
				s[7] = 7. * s[6] * h[n] / 8.;

				for(int i=0;i<N;i++) {					// Predict velocities at interval n using b values
					const int k0 = 3*i+0;
					const int k1 = 3*i+1;
					const int k2 = 3*i+2;

					double vk0 =  csv[k0] + s[7]*b[6][k0] + s[6]*b[5][k0] + s[5]*b[4][k0] + s[4]*b[3][k0] + s[3]*b[2][k0] + s[2]*b[1][k0] + s[1]*b[0][k0] + s[0]*a0[k0];
					particles[i].vx = vk0 + v0[k0];
					double vk1 =  csv[k1] + s[7]*b[6][k1] + s[6]*b[5][k1] + s[5]*b[4][k1] + s[4]*b[3][k1] + s[3]*b[2][k1] + s[2]*b[1][k1] + s[1]*b[0][k1] + s[0]*a0[k1];
					particles[i].vy = vk1 + v0[k1];
					double vk2 =  csv[k2] + s[7]*b[6][k2] + s[6]*b[5][k2] + s[5]*b[4][k2] + s[4]*b[3][k2] + s[3]*b[2][k2] + s[2]*b[1][k2] + s[1]*b[0][k2] + s[0]*a0[k2];
					particles[i].vz = vk2 + v0[k2];
				}
			}


			integrator_update_acceleration();				// Calculate forces at interval n

			for(int k=0;k<N;++k) {
				if (particles[k].dtexp != dtexp) continue;
				at[3*k]   = particles[k].ax;
				at[3*k+1] = particles[k].ay;  
				at[3*k+2] = particles[k].az;
			}
			switch (n) {							// Improve b and g values
				case 1: 
					for(int k=0;k<N3;++k) {
						if (particles[k/3].dtexp != dtexp) continue;
						double tmp = g[0][k];
						g[0][k]  = (at[k] - a0[k]) / r[0];
						b[0][k] += g[0][k] - tmp;
					} break;
				case 2: 
					for(int k=0;k<N3;++k) {
						if (particles[k/3].dtexp != dtexp) continue;
						double tmp = g[1][k];
						const double gk = at[k] - a0[k];
						g[1][k] = (gk/r[1] - g[0][k])/r[2];
						tmp = g[1][k] - tmp;
						b[0][k] += tmp * c[0];
						b[1][k] += tmp;
					} break;
				case 3: 
					for(int k=0;k<N3;++k) {
						if (particles[k/3].dtexp != dtexp) continue;
						double tmp = g[2][k];
						const double gk = at[k] - a0[k];
						g[2][k] = ((gk/r[3] - g[0][k])/r[4] - g[1][k])/r[5];
						tmp = g[2][k] - tmp;
						b[0][k] += tmp * c[1];
						b[1][k] += tmp * c[2];
						b[2][k] += tmp;
					} break;
				case 4:
					for(int k=0;k<N3;++k) {
						if (particles[k/3].dtexp != dtexp) continue;
						double tmp = g[3][k];
						const double gk = at[k] - a0[k];
						g[3][k] = (((gk/r[6] - g[0][k])/r[7] - g[1][k])/r[8] - g[2][k])/r[9];
						tmp = g[3][k] - tmp;
						b[0][k] += tmp * c[3];
						b[1][k] += tmp * c[4];
						b[2][k] += tmp * c[5];
						b[3][k] += tmp;
					} break;
				case 5:
					for(int k=0;k<N3;++k) {
						if (particles[k/3].dtexp != dtexp) continue;
						double tmp = g[4][k];
						const double gk = at[k] - a0[k];
						g[4][k] = ((((gk/r[10] - g[0][k])/r[11] - g[1][k])/r[12] - g[2][k])/r[13] - g[3][k])/r[14];
						tmp = g[4][k] - tmp;
						b[0][k] += tmp * c[6];
						b[1][k] += tmp * c[7];
						b[2][k] += tmp * c[8];
						b[3][k] += tmp * c[9];
						b[4][k] += tmp;
					} break;
				case 6:
					for(int k=0;k<N3;++k) {
						if (particles[k/3].dtexp != dtexp) continue;
						double tmp = g[5][k];
						const double gk = at[k] - a0[k];
						g[5][k] = (((((gk/r[15] - g[0][k])/r[16] - g[1][k])/r[17] - g[2][k])/r[18] - g[3][k])/r[19] - g[4][k])/r[20];
						tmp = g[5][k] - tmp;
						b[0][k] += tmp * c[10];
						b[1][k] += tmp * c[11];
						b[2][k] += tmp * c[12];
						b[3][k] += tmp * c[13];
						b[4][k] += tmp * c[14];
						b[5][k] += tmp;
					} break;
				case 7:
				{
					for(int k=0;k<N3;++k) {
						if (particles[k/3].dtexp != dtexp) continue;
						double tmp = g[6][k];
						const double gk = at[k] - a0[k];
						g[6][k] = ((((((gk/r[21] - g[0][k])/r[22] - g[1][k])/r[23] - g[2][k])/r[24] - g[3][k])/r[25] - g[4][k])/r[26] - g[5][k])/r[27];
						tmp = g[6][k] - tmp;	
						b[0][k] += tmp * c[15];
						b[1][k] += tmp * c[16];
						b[2][k] += tmp * c[17];
						b[3][k] += tmp * c[18];
						b[4][k] += tmp * c[19];
						b[5][k] += tmp * c[20];
						b[6][k] += tmp;
						
						// Monitor change in b[6][k] relative to at[k]. The predictor corrector scheme is converged if it is close to 0.
						const double ak  = at[k];
						const double b6ktmp = tmp; 
						const double errork = fabs(b6ktmp/ak);
						if (isnormal(errork) && errork>predictor_corrector_error){
							predictor_corrector_error = errork;
						}
					} 
					
					break;
				}
			}
		}
	}

	// Find new position and velocity values at end of the sequence
	const double dt_done2 = dt * dt;
	for(int i=0;i<N;i++) {
		if (particles[i].dtexp==dtexp){
			for (int k=3*i;k<3*(i+1);k++){
				{
					double a = x0[k];
					csx[k]  +=  (b[6][k]/72. + b[5][k]/56. + b[4][k]/42. + b[3][k]/30. + b[2][k]/20. + b[1][k]/12. + b[0][k]/6. + a0[k]/2.) 
							* dt_done2 + v0[k] * dt;
					x0[k]    = a + csx[k];
					csx[k]  += a - x0[k]; 
				}
				{
					double a = v0[k]; 
					csv[k]  += (b[6][k]/8. + b[5][k]/7. + b[4][k]/6. + b[3][k]/5. + b[2][k]/4. + b[1][k]/3. + b[0][k]/2. + a0[k])
							* dt;
					v0[k]    = a + csv[k];
					csv[k]  += a - v0[k];
				}

				// Swap particle buffers
				er[0][k] = e[0][k];
				er[1][k] = e[1][k];
				er[2][k] = e[2][k];
				er[3][k] = e[3][k];
				er[4][k] = e[4][k];
				er[5][k] = e[5][k];
				er[6][k] = e[6][k];
				
				br[0][k] = b[0][k];
				br[1][k] = b[1][k];
				br[2][k] = b[2][k];
				br[3][k] = b[3][k];
				br[4][k] = b[4][k];
				br[5][k] = b[5][k];
				br[6][k] = b[6][k];
			}
			particles[i].x = x0[3*i+0];	// Set final position
			particles[i].y = x0[3*i+1];
			particles[i].z = x0[3*i+2];
			

			particles[i].vx = v0[3*i+0];	// Set final velocity
			particles[i].vy = v0[3*i+1];
			particles[i].vz = v0[3*i+2];
			
			particles[i].tdone = t+dt;
			particles[i].dtdone = dt;
		}else{
			particles[i].x = x0[3*i]; 
			particles[i].y = x0[3*i+1]; 
			particles[i].z = x0[3*i+2]; 
		}
		particles[i].xpast[-dtexp][dtexp_substep[-dtexp]] = particles[i].x;	// Set final position
		particles[i].ypast[-dtexp][dtexp_substep[-dtexp]] = particles[i].y;
		particles[i].zpast[-dtexp][dtexp_substep[-dtexp]] = particles[i].z;
	}

	
	// Find new timestep
	
	if (integrator_epsilon>0){
		for(int i=0;i<N;i++) {
			if (particles[i].dtexp != dtexp) continue;
			double errork_max = 0.;
			for(int k=0;k<3;k++){
				const double ak  = at[3*i+k];
				const double b6k = b[6][3*i+k]; 
				const double errork = fabs(b6k/ak);
				if (errork>errork_max){
					errork_max = errork;
				}
			}

			if (isnormal(errork_max)){
				const double dtparticle = pow(integrator_epsilon/errork_max,1./7.)*dt;
				particles[i].dtexp = floor(log(dtparticle/integrator_max_dt)/log(8.));
				if (particles[i].dtexp>0){
					particles[i].dtexp = 0;
				}
				if (particles[i].dtexp<-2){
					particles[i].dtexp = -2;
				}
			}else{
				particles[i].dtexp = 0;
			}
		}
	}
	
	dtexp_min = 0;	
	for(int i=0;i<N;i++) {
		printf("min dt     %d\n",particles[i].dtexp);
		if (particles[i].dtexp<dtexp_min && isnormal(particles[i].dtexp)){
			dtexp_min = particles[i].dtexp;
		}
	}
	dtexp_substep[-dtexp]++;
	t += dt;
	if (dtexp_substep[-dtexp]==8){
		dtexp_substep[-dtexp]=0;
		dtexp++;
		if (dtexp>0){
			dtexp = dtexp_min;
		}else{
			double dtt = integrator_max_dt;
			for(int i=0;i<-dtexp;i++){
				int st = dtexp_substep[i];
				double st_dt = h[st+1]-h[st];
				dtt  *= st_dt;
			}
			t -= dtt;

		}
	}else{
		dtexp = dtexp_min;
	}
	return 1; // Success.
}

#ifdef GENERATE_CONSTANTS
void integrator_generate_constants(){
	printf("Generaring constants.\n\n");
	mpf_set_default_prec(512);
	mpf_t* _h = malloc(sizeof(mpf_t)*8);
	for (int i=0;i<8;i++){
		mpf_init(_h[i]);
	}
	mpf_t* _r = malloc(sizeof(mpf_t)*28);
	for (int i=0;i<28;i++){
		mpf_init(_r[i]);
	}
	mpf_t* _c = malloc(sizeof(mpf_t)*21);
	mpf_t* _d = malloc(sizeof(mpf_t)*21);
	for (int i=0;i<21;i++){
		mpf_init(_c[i]);
		mpf_init(_d[i]);
	}
	mpf_set_str(_h[0],"0.0",10);
	mpf_set_str(_h[1],"0.0562625605369221464656521910",10);
	mpf_set_str(_h[2],"0.1802406917368923649875799428",10);
	mpf_set_str(_h[3],"0.3526247171131696373739077702",10);
	mpf_set_str(_h[4],"0.5471536263305553830014485577",10);
	mpf_set_str(_h[5],"0.7342101772154105410531523211",10);
	mpf_set_str(_h[6],"0.8853209468390957680903597629",10);
	mpf_set_str(_h[7],"0.9775206135612875018911745004",10);

	int l=0;
	for (int j=1;j<8;++j) {
		for(int k=0;k<j;++k) {
			// r[l] = h[j] - h[k];
			mpf_sub(_r[l],_h[j],_h[k]);
			++l;
		}
	}
	//c[0] = -h[1];
	mpf_neg(_c[0],_h[1]);
	//d[0] =  h[1];
	mpf_set(_d[0],_h[1]);
	l=0;
	for (int j=2;j<7;++j) {
		++l;
		// c[l] = -h[j] * c[l-j+1];
		mpf_mul(_c[l], _h[j], _c[l-j+1]);
		mpf_neg(_c[l], _c[l]);
		//d[l] =  h[1] * d[l-j+1];
		mpf_mul(_d[l], _h[1], _d[l-j+1]);
		for(int k=2;k<j;++k) {
			++l;
			//c[l] = c[l-j] - h[j] * c[l-j+1];
			mpf_mul(_c[l], _h[j], _c[l-j+1]);
			mpf_sub(_c[l], _c[l-j], _c[l]);
			//d[l] = d[l-j] + h[k] * d[l-j+1];
			mpf_mul(_d[l], _h[k], _d[l-j+1]);
			mpf_add(_d[l], _d[l-j], _d[l]);
		}
		++l;
		//c[l] = c[l-j] - h[j];
		mpf_sub(_c[l], _c[l-j], _h[j]);
		//d[l] = d[l-j] + h[j]; 
		mpf_add(_d[l], _d[l-j], _h[j]);
	}

	// Output	
	printf("double r[28] = {");
	for (int i=0;i<28;i++){
	     gmp_printf ("%.*Ff", 25, _r[i]);
	     if (i!=27) printf(", ");
	}
	printf("};\n");
	printf("double c[21] = {");
	for (int i=0;i<21;i++){
	     gmp_printf ("%.*Ff", 25, _c[i]);
	     if (i!=20) printf(", ");
	}
	printf("};\n");
	printf("double d[21] = {");
	for (int i=0;i<21;i++){
	     gmp_printf ("%.*Ff", 25, _d[i]);
	     if (i!=20) printf(", ");
	}
	printf("};\n");
	exit(0);
}
#endif // GENERATE_CONSTANTS
