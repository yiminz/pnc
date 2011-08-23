/**
 *	Simulation of PNC-ZF using IT++
 *	Two antennas in the relay
 *
 */

#include <itpp/itstat.h>
#include <itpp/itcomm.h>

#include "mimomac.h"
#include "zftwrelay.h"

using namespace itpp;
using namespace std;

Array<cvec> to_mimo_input(Array<cvec> &user_input) {
	
	int sym_len = user_input(0).size();
	Array<cvec> mimo_in(sym_len);
	for(int i=0; i<sym_len; i++) {
		cvec in(user_input.size());
		for(int k=0; k<user_input.size(); k++) {
			in(k) = user_input(k).get(i);
		}
		mimo_in(i) = in;
	}
	
	return mimo_in;
}

int sym_err(bvec bv_src, ivec iv_dem) {
	int err = 0;
	for(int i=0, k=0; i<iv_dem.size(); i++, k+=2) {
		int src = (bv_src(k).value()<<1) + bv_src(k+1).value();
		if(src!=iv_dem(i))
			err++;
	}
	return err;
}

double norm2_a_pinvH(vec a, cmat pinvH) {
	cvec ca = to_cvec(a);
	cmat pinvHt = transpose(pinvH);
	cvec res = pinvHt * ca;
	return energy(res);
}


int main(int argc, char *argv[]) 
{
	// Fixed channel matrix from file
	cmat read_H;
	bool is_fixed_H = false;
	if(argc>=2) {
		it_file ff;
		ff.open(argv[1]);
		ff>>Name("H")>>read_H;
		ff.close();	
		cout<<"Read H: "<<read_H<<endl;
		is_fixed_H = true;
	}
	
	// Fixed a
	vec a;
	a.set_size(2);
	bool is_assigned_a = false;
	if(argc>=4) {
		a(0) = atoi(argv[2]);
		a(1) = atoi(argv[3]);
		is_assigned_a = true;
	}

	RNG_randomize();
	Real_Timer tt;

	char buf[1024];

	// log file
	ofstream log;
	log.open("log.txt", ios::app|ios::out);
	log<<"==================================================================================================="<<endl;

	//////////////////////////////////////////////////
	// Simulation parameters
	//////////////////////////////////////////////////
	int num_user = 2;
	int num_rx_ant = 2;

	int block_num = 100;
	int msg_len = 10000;

	vec EsN0dB  = linspace(11,20,10);	
	

	if(!is_fixed_H) {
		block_num = 10000;
		msg_len = 100;	
		EsN0dB  = linspace(11,30,20);	// fading
	}

	int sym_len = msg_len/2;  // QPSK

	double Es = 1;

	

	vec EsN0    = inv_dB(EsN0dB);
	vec N0      = Es * pow(EsN0, -1.0);
	vec sqrt_N0 = sqrt(N0);
	vec sigma2  = N0/2;
	vec sigma   = sqrt(sigma);
	
	cout<<sigma<<endl;


	bvec bv_msg_u1, bv_msg_u2;
	bvec bv_msg_xor;
	
	cvec cv_tx_sym_u1, cv_tx_sym_u2;
	cvec cv_rx_sym;
	

	
	/////////////////////////////////////////////////
	// Users' modulators
	/////////////////////////////////////////////////
	QAM qam(4);                     //The 4-QAM modulator class
	cvec syms = qam.get_symbols();
	//cout<<"4-QAM constellation: "<<syms<<endl;

	/////////////////////////////////////////////////
	// Channel initialization
	/////////////////////////////////////////////////
	MimoMac mimomac(num_user, num_rx_ant);
	if(!is_fixed_H)
		mimomac.genH();
	else
		mimomac.set_H(read_H);
	cmat H = mimomac.get_H();
	cout<<"H="<<H<<endl;
	log<<"H=\n"<<H<<endl;

	/////////////////////////////////////////////////
	// PNC Relay
	// Demapping region (x1 + x2)
	/////////////////////////////////////////////////
	ZfTwRelay relay;
	relay.set_H(H);
	if(!is_assigned_a) {
		a = relay.calc_opt_lincoeff();
	}
	relay.init_dem_region(a, syms, syms);
	cmat pinvH = relay.get_pinvH();
//	relay.show_sp_constellation();
//	relay.show_dem_regions();

	double norm2 = norm2_a_pinvH(a, pinvH);
	cout<<"a="<<a<<endl;
	log<<"a="<<a<<endl;
	log<<"pinvH=\n"<<pinvH<<endl;
	log<<"||a pinvH||^2 = "<<norm2<<endl;


	/////////////////////////////////////////////////
	// Simulation
	/////////////////////////////////////////////////
	log<<"#EsN0dB       #err         SER"<<endl;
	for(int i=0; i<EsN0dB.size(); i++) {

		int tot_sym = 0, err = 0;
		for(int bk=1; bk<=block_num; bk++) {

			//======================================
			// generate message bits
			//======================================
			bv_msg_u1 = randb(msg_len);
			bv_msg_u2 = randb(msg_len);
			bv_msg_xor = bv_msg_u1 + bv_msg_u2;

			//======================================
			// modulation
			//======================================
			cvec cv_txsig_u1 = qam.modulate_bits(bv_msg_u1);
			cvec cv_txsig_u2 = qam.modulate_bits(bv_msg_u2);

			//======================================
			// prepare proper format for mimo input
			//======================================
			Array<cvec> user_input(2);
			user_input(0) = cv_txsig_u1;
			user_input(1) = cv_txsig_u2;
			Array<cvec> mimo_input = to_mimo_input(user_input);

			//======================================
			// channel
			//======================================
			if(!is_fixed_H) {
				mimomac.genH();
				H = mimomac.get_H();
				relay.set_H(H);
				a = relay.calc_opt_lincoeff();
				relay.init_dem_region(a, syms, syms);
			}
			mimomac.set_N0(N0(i));
			Array<cvec> mimo_output = mimomac.channel(mimo_input);


			//======================================
			// PNC Demapping
			//======================================
			ivec dem_sym = relay.pnc_demapping(mimo_output);

			// cout<<"bv_msg_xor="<<bv_msg_xor<<endl;
			// cout<<"dem_sym="<<dem_sym<<endl;

			err += sym_err(bv_msg_xor, dem_sym);

			tot_sym += sym_len;
			printf("EsN0dB=%4.1f, #bk=%6i, #err=%6i, SER=%1.3e\r",
					EsN0dB(i), bk, err, ((double)err)/tot_sym);


			// cout<<bv_msg_u1<<endl;
			// cout<<cv_txsig_u1<<endl;

			// cout<<bv_msg_u2<<endl;
			// cout<<cv_txsig_u2<<endl;

			// cout<<user_input<<endl;
			// cout<<mimo_input<<endl;

			// cout<<mimo_output<<endl;

		}
		printf("\n");

		// log
		sprintf(buf, "  %4.1f      %6i    %1.3e", EsN0dB(i), err, ((double)err)/tot_sym);
		log<<buf<<endl;
	}

	log<<"===================================================================================================\n\n"<<endl;
	log.close();


	return 0;
}
