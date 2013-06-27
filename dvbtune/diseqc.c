#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include <linux/dvb/frontend.h>
#include "diseqc.h"

struct diseqc_cmd committed_switch_cmds[] = {
	{ { { 0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf2, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf1, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf3, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf4, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf6, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf5, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf7, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf8, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xfa, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xf9, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xfb, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xfc, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xfe, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xfd, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x38, 0xff, 0x00, 0x00 }, 4 }, 20 }
};

struct diseqc_cmd uncommitted_switch_cmds[] = {
	{ { { 0xe0, 0x10, 0x39, 0xf0, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf1, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf2, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf3, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf4, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf5, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf6, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf7, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf8, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xf9, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xfa, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xfb, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xfc, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xfd, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xfe, 0x00, 0x00 }, 4 }, 20 },
	{ { { 0xe0, 0x10, 0x39, 0xff, 0x00, 0x00 }, 4 }, 20 }
};

/*--------------------------------------------------------------------------*/

static inline void msleep(uint32_t msec)
{
	struct timespec req = { msec / 1000, 1000000 * (msec % 1000) };

	while (nanosleep(&req, &req))
		;
}

#if 0
#define DISEQC_X 2
int rotor_command( int frontend_fd, int cmd, int n1, int n2, int n3 )
{
	int err;
        struct dvb_diseqc_master_cmd cmds[] = {
                { { 0xe0, 0x31, 0x60, 0x00, 0x00, 0x00 }, 3 },  //0 Stop Positioner movement
                { { 0xe0, 0x31, 0x63, 0x00, 0x00, 0x00 }, 3 },  //1 Disable Limits
                { { 0xe0, 0x31, 0x66, 0x00, 0x00, 0x00 }, 3 },  //2 Set East Limit
                { { 0xe0, 0x31, 0x67, 0x00, 0x00, 0x00 }, 3 },  //3 Set West Limit
                { { 0xe0, 0x31, 0x68, 0x00, 0x00, 0x00 }, 4 },  //4 Drive Motor East continously
                { { 0xe0, 0x31, 0x68,256-n1,0x00, 0x00 }, 4 },  //5 Drive Motor East nn steps
                { { 0xe0, 0x31, 0x69,256-n1,0x00, 0x00 }, 4 },  //6 Drive Motor West nn steps
                { { 0xe0, 0x31, 0x69, 0x00, 0x00, 0x00 }, 4 },  //7 Drive Motor West continously
                { { 0xe0, 0x31, 0x6a, n1, 0x00, 0x00 }, 4 },  //8 Store nn
                { { 0xe0, 0x31, 0x6b, n1, 0x00, 0x00 }, 4 },   //9 Goto nn
                { { 0xe0, 0x31, 0x6f, n1, n2, n3 }, 4}, //10 Recalculate Position
                { { 0xe0, 0x31, 0x6a, 0x00, 0x00, 0x00 }, 4 },  //11 Enable Limits
                { { 0xe0, 0x31, 0x6e, n1, n2, 0x00 }, 5 },   //12 Gotoxx
                { { 0xe0, 0x10, 0x38, 0xF4, 0x00, 0x00 }, 4 }    //13 User
        };

        int i;
        for ( i=0; i<DISEQC_X; ++i ) {
                usleep(15*1000);
                if ( err = ioctl( frontend_fd, FE_DISEQC_SEND_MASTER_CMD, &cmds[cmd] ) )
                        error("rotor_command: FE_DISEQC_SEND_MASTER_CMD failed, err=%i\n",err);
        }
	return err;
}

int rotate_rotor (int frontend_fd, int from_rotor_pos, int to_rotor_pos, int voltage_18, int hiband){
	/* Rotate a DiSEqC 1.2 rotor from position from_rotor_pos to position to_rotor_pos */
	/* Uses Goto nn (command 9) */
	float rotor_wait_time; //seconds
	int err=0;

	float speed_13V = 1.5; //degrees per second
	float speed_18V = 2.4; //degrees per second
	float degreesmoved,a1,a2;

	if (to_rotor_pos != 0) {
		if (from_rotor_pos != to_rotor_pos) {
			info("Moving rotor from position %i to position %i\n",from_rotor_pos,to_rotor_pos);
			if (from_rotor_pos == 0) {
				rotor_wait_time = 15; // starting from unknown position
			} else {
				a1 = rotor_angle(to_rotor_pos);
				a2 = rotor_angle(from_rotor_pos);
				degreesmoved = abs(a1-a2);
				if (degreesmoved>180) degreesmoved=360-degreesmoved;
				rotor_wait_time = degreesmoved / speed_18V;
			}

			//switch tone off
			if (err = ioctl(frontend_fd, FE_SET_TONE, SEC_TONE_OFF))
				return err;
			msleep(15);
			// high voltage for high speed rotation
			if (err = ioctl(frontend_fd, FE_SET_VOLTAGE, SEC_VOLTAGE_18))
				return err;
			msleep(15);
			err = rotor_command(frontend_fd, 9, to_rotor_pos, 0, 0);
			if (err) {
				info("Rotor move error!\n");
			} else {
				int i;
				info("Rotating");
				for (i=0; i<10; i++){
					usleep(rotor_wait_time*100000);
					info(".");
				}
				info("completed.\n");
			}

		} else {
			info("Rotor already at position %i\n", from_rotor_pos);
		}

		// correct tone and voltage
		if (err = ioctl(frontend_fd, FE_SET_TONE, hiband ? SEC_TONE_ON : SEC_TONE_OFF))
                        return err;
		msleep(15);
		if (err = ioctl(frontend_fd, FE_SET_VOLTAGE, voltage_18))
			return err;
		msleep(15);
	}
	return err;
}
#endif


int diseqc_send_msg (int fd, fe_sec_voltage_t v, struct diseqc_cmd **cmd,
					 fe_sec_tone_mode_t t, fe_sec_mini_cmd_t b)
{
	int err;

	if ((err = ioctl(fd, FE_SET_TONE, SEC_TONE_OFF)))
		return err;

	if ((err = ioctl(fd, FE_SET_VOLTAGE, v)))
		return err;

	msleep(15);

	while (*cmd) {
	  //		fprintf(stderr,"DiSEqC: %02x %02x %02x %02x %02x %02x\n",
	  //			(*cmd)->cmd.msg[0], (*cmd)->cmd.msg[1],
	  //			(*cmd)->cmd.msg[2], (*cmd)->cmd.msg[3],
	  //			(*cmd)->cmd.msg[4], (*cmd)->cmd.msg[5]);

		if ((err = ioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &(*cmd)->cmd)))
			return err;

		//		msleep((*cmd)->wait);
		cmd++;
	}

	//fprintf(stderr," %s ", v == SEC_VOLTAGE_13 ? "SEC_VOLTAGE_13" :
	//    v == SEC_VOLTAGE_18 ? "SEC_VOLTAGE_18" : "???");

	//fprintf(stderr," %s ", b == SEC_MINI_A ? "SEC_MINI_A" :
	//    b == SEC_MINI_B ? "SEC_MINI_B" : "???");

	//fprintf(stderr," %s\n", t == SEC_TONE_ON ? "SEC_TONE_ON" :
	//    t == SEC_TONE_OFF ? "SEC_TONE_OFF" : "???");

	msleep(15);

	if ((err = ioctl(fd, FE_DISEQC_SEND_BURST, b)))
		return err;

	msleep(15);

	err = ioctl(fd, FE_SET_TONE, t);

	msleep(15);

	return err;
}

int setup_switch (int frontend_fd, int switch_pos, int voltage_18, int hiband, int uncommitted_switch_pos)
{
	int i;
	int err;
	struct diseqc_cmd *cmd[2] = { NULL, NULL };

	i = uncommitted_switch_pos;

	//	fprintf(stderr,"DiSEqC: uncommitted switch pos %i\n", uncommitted_switch_pos);
	if (i < 0 || i >= (int) (sizeof(uncommitted_switch_cmds)/sizeof(struct diseqc_cmd)))
		return -EINVAL;

	cmd[0] = &uncommitted_switch_cmds[i];

	diseqc_send_msg (frontend_fd,
		voltage_18 ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13,
		cmd,
		hiband ? SEC_TONE_ON : SEC_TONE_OFF,
		switch_pos % 2 ? SEC_MINI_B : SEC_MINI_A);

	i = 4 * switch_pos + 2 * hiband + (voltage_18 ? 1 : 0);

	//	fprintf(stderr,"DiSEqC: switch pos %i, %sV, %sband (index %d)\n",
	//		switch_pos, voltage_18 ? "18" : "13", hiband ? "hi" : "lo", i);

	if (i < 0 || i >= (int) (sizeof(committed_switch_cmds)/sizeof(struct diseqc_cmd)))
		return -EINVAL;

	cmd[0] = &committed_switch_cmds[i];

	err = diseqc_send_msg (frontend_fd,
		voltage_18 ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13,
		cmd,
		hiband ? SEC_TONE_ON : SEC_TONE_OFF,
		switch_pos % 2 ? SEC_MINI_B : SEC_MINI_A);

	return err;
}


