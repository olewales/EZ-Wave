/* -*- c++ -*- */
/*
 * Copyright 2013 Airbus DS CyberSecurity.
 * Authors: Jean-Michel Picod, Arnaud Lebrun, Jonathan Christofer Demay
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "preamble_impl.h"
#include <string.h>
#include <gnuradio/block_detail.h>

#define ZWAVE 0x01
//define PREAMBLE_SIZE 25 // <<<< of preamble table size
int PREAMBLE_SIZE=25;

namespace gr {
  namespace Zwave {

    preamble::sptr
    preamble::make(int preamble_length)
    {
      return gnuradio::get_initial_sptr
        (new preamble_impl(preamble_length));
    }

    //Construtor
    preamble_impl::preamble_impl(int preamble_length)
      : gr::block("preamble",
        gr::io_signature::make(0, 0, 0),
        gr::io_signature::make(0, 0, 0))
{
//    int jojo=0;
//   for(;jojo<PREAMBLE_SIZE;jojo++) preamble[jojo]=0x55;
//    preamble[jojo]=0xF0;

		set_preamble(preamble_length);

    //Queue stuff
    message_port_register_out(pmt::mp("out"));
    message_port_register_in(pmt::mp("in"));
    set_msg_handler(pmt::mp("in"), boost::bind(&preamble_impl::general_work, this, _1));

}

    //Destructor
    preamble_impl::~preamble_impl()
    {
    }

void preamble_impl::set_preamble(int preamble_length){
        if (preamble_length % 8 > 0){
            PREAMBLE_SIZE = int(preamble_length / 8) + 1;
        }
        else{
            PREAMBLE_SIZE = int(preamble_length / 8);
        }

        int jojo=0;
        for(;jojo<PREAMBLE_SIZE;jojo++){
            if (preamble_length <= ((PREAMBLE_SIZE - (jojo+1)) * 8)){
            preamble[jojo]=0x00;
            }
            else if (preamble_length < ((PREAMBLE_SIZE - jojo) * 8)){
                int shift = (preamble_length % 8);
                preamble[jojo] = 0x55 & ((0xFF << shift)^0xFF);
                /*
                if (shift % 2 == 0){
                    preamble[jojo] = 0x55 & ((0xFF << shift)^0xFF);
                }
                else{
                    preamble[jojo] = (0x55 & ((0xFF << shift)^0xFF) | 0x1 << shift);
                }*/
            }
            else{ //preamble_length >= ((PREAMBLE_SIZE - (jojo+1)) * 8)
                preamble[jojo] = 0x55;
            }
        }

        //preamble[jojo]=0xF0;
				preamble[PREAMBLE_SIZE]=0xF0;
    }

    // """main""" function
void preamble_impl::general_work (pmt::pmt_t msg){

	if(pmt::is_eof_object(msg)) {
		message_port_pub(pmt::mp("out"), pmt::PMT_EOF);
		detail().get()->set_done(true);
		return;
	}

	assert(pmt::is_pair(msg));
	pmt::pmt_t blob = pmt::cdr(msg);
	beam beamFrame = beam();

	size_t data_len = pmt::blob_length(blob);
	uint8_t numBeams;

	assert(data_len);
	//std::cout << "data_len is > 0\n";
	assert(data_len < (256 - 1 - 1 - PREAMBLE_SIZE)); //CWB: Need to account for preamble size, SoF, and 1-byte padding, else overflow
	//std::cout << "data_len is < 256\n";

	//Check if Zwave frame
    char temp[256];
		char *rxZwaveFrame;
		char *rxScapyFrame;
		char *pkZwaveFrame = &preamble[PREAMBLE_SIZE+1];
		const size_t scapyFrameLen = 8;
		size_t ZwaveFrameLen;

    std::memcpy(temp, pmt::blob_data(blob), data_len);
		rxScapyFrame = temp;
		rxZwaveFrame = &temp[scapyFrameLen];
		ZwaveFrameLen = data_len - scapyFrameLen;

    if(rxScapyFrame[0] == ZWAVE)
			{
				//std::cout << "Is Zwave frame\n";

				numBeams = rxScapyFrame[1];
				//std::cout << "Generating " << (uint16_t)numBeams << " beams\n";

				if ( (uint16_t)numBeams > 0) 
					{
						beamFrame.nodeid = rxScapyFrame[2];

						// CWB: After playing with the padding for the singlecast frame, I realized that it is needed to avoid checksum errors. We may
						// need to consider if we should pad each beam frame as well. Currently the last byte is the target to wakeup.
						// TODO: Observe if the beam's last byte is getting mangled, necessitating a padding byte.
						std::cout << "Attempting to wakeup " << (uint16_t)rxScapyFrame[2] << " with " << (uint16_t)numBeams << " beam frames." << std::endl;
						pmt::pmt_t packet = pmt::make_blob (&beamFrame, sizeof(beam));

						for (uint16_t i=0;i<(uint16_t)numBeams;i++) // Send beams
							{
								message_port_pub(pmt::mp("out"), pmt::cons(pmt::PMT_NIL, packet));
							}
					}
		
				//std::cout << "Copying rcved frame to preamble buffer\n";
		
				// PREAMBLE_SIZE preamble + 1 byte SOF. Also remove scapyhdr from tmp
				std::memcpy (pkZwaveFrame, rxZwaveFrame, ZwaveFrameLen);

				//std::memcpy(preamble + 1 + PREAMBLE_SIZE, pmt::blob_data(blob)+8, data_len-8); // blob_data+1 to remove the 2 byte header

				// CWB: It appears as though the last byte of the frame gets slightly mangled. To avoid large checksum errors, the frame is padded by at least one byte
	    	//preamble[data_len+1+PREAMBLE_SIZE-8] = 0xAA;
				preamble[PREAMBLE_SIZE + 1 + ZwaveFrameLen] = 0xAA;

//    	for(int toto=0;toto< (PREAMBLE_SIZE+data_len-8+2);toto++)  preamble[toto] ^=  0xff;

				//CWB: does pmt::make_blob use static memory or heap? Do we free it or does the sdr sink do it?
				//pmt::pmt_t packet = pmt::make_blob(preamble, data_len-8 + 1+1+PREAMBLE_SIZE); //padding of 1 octets
				//std::cout << "Sending PHY Frame: " << std::hex;
				//for (int i=0;i<(PREAMBLE_SIZE+1+ZwaveFrameLen);i++)
				//	std::cout << " " << std::setfill('0') << std::setw(2) << (uint16_t)preamble[i];
			
				std::cout << std::endl;
				pmt::pmt_t packet = pmt::make_blob (preamble, PREAMBLE_SIZE + 1 + ZwaveFrameLen + 1);

				//std::cout << "Sending out port\n";
				message_port_pub(pmt::mp("out"), pmt::cons(pmt::PMT_NIL, packet));
		}
 }

  } /* namespace Zwave */
} /* namespace gr */
