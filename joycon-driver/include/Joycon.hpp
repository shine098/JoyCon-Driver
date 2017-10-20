#include <bitset>
#include <hidapi.h>
#include "tools.hpp"

class Joycon {

public:

	hid_device *handle;
	wchar_t *serial;

	std::string name;

	bool bluetooth = true;

	int left_right = 0;// 1: left joycon, 2: right joycon, 3: pro controller

	uint16_t buttons = 0;
	uint16_t buttons2 = 0;// for pro controller

	int8_t dstick;
	uint8_t battery;

	int global_count = 0;


	int timing_byte = 0x0;

	struct Stick {
		uint16_t x = 0;
		uint16_t y = 0;
		float CalX = 0;
		float CalY = 0;
	};

	Stick stick;
	Stick stick2;// Pro Controller

	struct Gyroscope {
		// absolute:
		double pitch	= 0;
		double yaw		= 0;
		double roll		= 0;

		// relative:
		double relpitch = 0;
		double relyaw	= 0;
		double relroll	= 0;

	} gyro;

	struct Accelerometer {
		double prevX = 0;
		double prevY = 0;
		double prevZ = 0;
		double x = 0;
		double y = 0;
		double z = 0;
	} accel;


	// calibration data:

	struct brcm_hdr {
		uint8_t cmd;
		uint8_t rumble[9];
	};

	//struct brcm_cmd_01 {
	//	uint8_t subcmd;
	//	union {

	//		struct {
	//			uint32_t offset;
	//			uint8_t size;
	//		} spi_read;

	//		struct {
	//			uint32_t address;
	//		} hax_read;
	//	};
	//};

	struct brcm_cmd_01 {
		uint8_t subcmd;
		uint32_t offset;
		uint8_t size;
	};

	float acc_cal_coeff[3];
	float gyro_cal_coeff[3];
	float cal_x[1] = { 0.0f };
	float cal_y[1] = { 0.0f };

	bool has_user_cal_stick_l = false;
	bool has_user_cal_stick_r = false;
	bool has_user_cal_sensor = false;

	unsigned char factory_stick_cal[0x12];
	unsigned char user_stick_cal[0x16];
	unsigned char sensor_model[0x6];
	unsigned char stick_model[0x24];
	unsigned char factory_sensor_cal[0x18];
	unsigned char user_sensor_cal[0x1A];
	uint16_t factory_sensor_cal_calm[0xC];
	uint16_t user_sensor_cal_calm[0xC];
	int16_t sensor_cal[0x2][0x3];
	uint16_t stick_cal_x_l[0x3];
	uint16_t stick_cal_y_l[0x3];
	uint16_t stick_cal_x_r[0x3];
	uint16_t stick_cal_y_r[0x3];


public:

	void hid_exchange(hid_device *handle, unsigned char *buf, int len) {
		if (!handle) return;

		int res;

		res = hid_write(handle, buf, len);

		//if (res < 0) {
		//	printf("Number of bytes written was < 0!\n");
		//} else {
		//	printf("%d bytes written.\n", res);
		//}

		//// set non-blocking:
		//hid_set_nonblocking(handle, 1);

		res = hid_read(handle, buf, 0x40);

		//if (res < 1) {
		//	printf("Number of bytes read was < 1!\n");
		//} else {
		//	printf("%d bytes read.\n", res);
		//}
	}


	void send_command(int command, uint8_t *data, int len) {
		unsigned char buf[0x40];
		memset(buf, 0, 0x40);

		if (!bluetooth) {
			buf[0x00] = 0x80;
			buf[0x01] = 0x92;
			buf[0x03] = 0x31;
		}

		buf[bluetooth ? 0x0 : 0x8] = command;
		if (data != nullptr && len != 0) {
			memcpy(buf + (bluetooth ? 0x1 : 0x9), data, len);
		}

		hid_exchange(this->handle, buf, len + (bluetooth ? 0x1 : 0x9));

		if (data) {
			memcpy(data, buf, 0x40);
		}
	}

	void send_subcommand(int command, int subcommand, uint8_t *data, int len) {
		unsigned char buf[0x40];
		memset(buf, 0, 0x40);

		uint8_t rumble_base[9] = { (++global_count) & 0xF, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40 };
		memcpy(buf, rumble_base, 9);

		if (global_count > 0xF) {
			global_count = 0x0;
		}

		// set neutral rumble base only if the command is vibrate (0x01)
		// if set when other commands are set, might cause the command to be misread and not executed
		//if (subcommand == 0x01) {
		//	uint8_t rumble_base[9] = { (++global_count) & 0xF, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40 };
		//	memcpy(buf + 10, rumble_base, 9);
		//}

		buf[9] = subcommand;
		if (data && len != 0) {
			memcpy(buf + 10, data, len);
		}

		send_command(command, buf, 10 + len);

		if (data) {
			memcpy(data, buf, 0x40); //TODO
		}
	}

	void rumble(int frequency, int intensity) {

		unsigned char buf[0x400];
		memset(buf, 0, 0x40);

		// intensity: (0, 8)
		// frequency: (0, 255)

		//	 X	AA	BB	 Y	CC	DD
		//[0 1 x40 x40 0 1 x40 x40] is neutral.


		//for (int j = 0; j <= 8; j++) {
		//	buf[1 + intensity] = 0x1;//(i + j) & 0xFF;
		//}

		buf[1 + 0 + intensity] = 0x1;
		buf[1 + 4 + intensity] = 0x1;

		// Set frequency to increase
		if (this->left_right == 1) {
			buf[1 + 0] = frequency;// (0, 255)
		} else {
			buf[1 + 4] = frequency;// (0, 255)
		}

		// set non-blocking:
		hid_set_nonblocking(this->handle, 1);

		send_command(0x10, (uint8_t*)buf, 0x9);
	}

	void rumble2(uint16_t hf, uint8_t hfa, uint8_t lf, uint16_t lfa) {
		unsigned char buf[0x400];
		memset(buf, 0, 0x40);


		//int hf		= HF;
		//int hf_amp	= HFA;
		//int lf		= LF;
		//int lf_amp	= LFA;
		// maybe:
		//int hf_band = hf + hf_amp;

		int off = 0;// offset
		if (this->left_right == 2) {
			off = 4;
		}


		// Byte swapping
		buf[0 + off] = hf & 0xFF;
		buf[1 + off] = hfa + ((hf >> 8) & 0xFF); //Add amp + 1st byte of frequency to amplitude byte

												 // Byte swapping
		buf[2 + off] = lf + ((lfa >> 8) & 0xFF); //Add freq + 1st byte of LF amplitude to the frequency byte
		buf[3 + off] = lfa & 0xFF;


		// set non-blocking:
		hid_set_nonblocking(this->handle, 1);

		send_command(0x10, (uint8_t*)buf, 0x9);
	}

	void rumble3(float frequency, uint8_t hfa, uint16_t lfa) {

		//Float frequency to hex conversion
		if (frequency < 0.0f) {
			frequency = 0.0f;
		} else if (frequency > 1252.0f) {
			frequency = 1252.0f;
		}
		uint8_t encoded_hex_freq = (uint8_t)round(log2((double)frequency / 10.0)*32.0);

		//uint16_t encoded_hex_freq = (uint16_t)floor(-32 * (0.693147f - log(frequency / 5)) / 0.693147f + 0.5f); // old

		//Convert to Joy-Con HF range. Range in big-endian: 0x0004-0x01FC with +0x0004 steps.
		uint16_t hf = (encoded_hex_freq - 0x60) * 4;
		//Convert to Joy-Con LF range. Range: 0x01-0x7F.
		uint8_t lf = encoded_hex_freq - 0x40;

		rumble2(hf, hfa, lf, lfa);
	}



	void rumble4(float real_LF, float real_HF, uint8_t hfa, uint16_t lfa) {

		real_LF = clamp(real_LF, 40.875885f, 626.286133f);
		real_HF = clamp(real_HF, 81.75177, 1252.572266f);

		////Float frequency to hex conversion
		//if (frequency < 0.0f) {
		//	frequency = 0.0f;
		//} else if (frequency > 1252.0f) {
		//	frequency = 1252.0f;
		//}
		//uint8_t encoded_hex_freq = (uint8_t)round(log2((double)frequency / 10.0)*32.0);

		//uint16_t encoded_hex_freq = (uint16_t)floor(-32 * (0.693147f - log(frequency / 5)) / 0.693147f + 0.5f); // old

		////Convert to Joy-Con HF range. Range in big-endian: 0x0004-0x01FC with +0x0004 steps.
		//uint16_t hf = (encoded_hex_freq - 0x60) * 4;
		////Convert to Joy-Con LF range. Range: 0x01-0x7F.
		//uint8_t lf = encoded_hex_freq - 0x40;



		uint16_t hf = ((uint8_t)round(log2((double)real_HF * 0.01)*32.0) - 0x60) * 4;
		uint8_t lf = (uint8_t)round(log2((double)real_LF * 0.01)*32.0) - 0x40;

		rumble2(hf, hfa, lf, lfa);
	}


	void rumble_freq(uint16_t hf, uint8_t hfa, uint8_t lf, uint16_t lfa) {
		unsigned char buf[0x400];
		memset(buf, 0, 0x40);


		//int hf		= HF;
		//int hf_amp	= HFA;
		//int lf		= LF;
		//int lf_amp	= LFA;
		// maybe:
		//int hf_band = hf + hf_amp;

		int off = 0;// offset
		if (this->left_right == 2) {
			off = 4;
		}


		// Byte swapping
		buf[0 + off] = hf & 0xFF;
		buf[1 + off] = hfa + ((hf >> 8) & 0xFF); //Add amp + 1st byte of frequency to amplitude byte

												 // Byte swapping
		buf[2 + off] = lf + ((lfa >> 8) & 0xFF); //Add freq + 1st byte of LF amplitude to the frequency byte
		buf[3 + off] = lfa & 0xFF;


		// set non-blocking:
		hid_set_nonblocking(this->handle, 1);

		send_command(0x10, (uint8_t*)buf, 0x9);
	}


	int init_bt() {

		this->bluetooth = true;

		unsigned char buf[0x40];
		memset(buf, 0, 0x40);



		// set non-blocking:
		//hid_set_nonblocking(this->handle, 1);
		// set blocking to ensure command is recieved:
		hid_set_nonblocking(this->handle, 0);

		// get calibration data:
		printf("Getting calibration data...\n");
		memset(factory_stick_cal, 0, 0x12);
		memset(user_stick_cal, 0, 0x16);
		memset(sensor_model, 0, 0x6);
		memset(stick_model, 0, 0x12);
		memset(factory_sensor_cal, 0, 0x18);
		memset(user_sensor_cal, 0, 0x1A);
		memset(factory_sensor_cal_calm, 0, 0xC);
		memset(user_sensor_cal_calm, 0, 0xC);
		memset(sensor_cal, 0, sizeof(sensor_cal));
		memset(stick_cal_x_l, 0, sizeof(stick_cal_x_l));
		memset(stick_cal_y_l, 0, sizeof(stick_cal_y_l));
		memset(stick_cal_x_r, 0, sizeof(stick_cal_x_r));
		memset(stick_cal_y_r, 0, sizeof(stick_cal_y_r));


		get_spi_data(0x6020, 0x18, factory_sensor_cal);
		get_spi_data(0x603D, 0x12, factory_stick_cal);
		get_spi_data(0x6080, 0x6, sensor_model);
		get_spi_data(0x6086, 0x12, stick_model);
		get_spi_data(0x6098, 0x12, &stick_model[0x12]);
		get_spi_data(0x8010, 0x16, user_stick_cal);
		get_spi_data(0x8026, 0x1A, user_sensor_cal);

		// set blocking to ensure command is recieved:
		hid_set_nonblocking(this->handle, 0);

		// Enable vibration
		printf("Enabling vibration...\n");
		buf[0] = 0x01; // Enabled
		send_subcommand(0x1, 0x48, buf, 1);

		// Enable IMU data
		printf("Enabling IMU data...\n");
		buf[0] = 0x01; // Enabled
		send_subcommand(0x01, 0x40, buf, 1);


		// Set input report mode (to push at 60hz)
		// x00	Active polling mode for IR camera data. Answers with more than 300 bytes ID 31 packet
		// x01	Active polling mode
		// x02	Active polling mode for IR camera data.Special IR mode or before configuring it ?
		// x21	Unknown.An input report with this ID has pairing or mcu data or serial flash data or device info
		// x23	MCU update input report ?
		// 30	NPad standard mode. Pushes current state @60Hz. Default in SDK if arg is not in the list
		// 31	NFC mode. Pushes large packets @60Hz
		printf("Set input report mode to 0x30...\n");
		buf[0] = 0x30;
		send_subcommand(0x01, 0x03, buf, 1);


		printf("Successfully initialized %s!\n", this->name.c_str());

		return 0;
	}

	void init_usb() {

		this->bluetooth = false;

		unsigned char buf[0x400];
		memset(buf, 0, 0x400);

		// set blocking:
		// this insures we get the MAC Address
		hid_set_nonblocking(this->handle, 0);

		//Get MAC Left
		printf("Getting MAC...\n");
		memset(buf, 0x00, 0x40);
		buf[0] = 0x80;
		buf[1] = 0x01;
		hid_exchange(this->handle, buf, 0x2);

		if (buf[2] == 0x3) {
			printf("%s disconnected!\n", this->name.c_str());
		} else {
			printf("Found %s, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", this->name.c_str(), buf[9], buf[8], buf[7], buf[6], buf[5], buf[4]);
		}

		// set non-blocking:
		//hid_set_nonblocking(jc->handle, 1);

		// Do handshaking
		printf("Doing handshake...\n");
		memset(buf, 0x00, 0x40);
		buf[0] = 0x80;
		buf[1] = 0x02;
		hid_exchange(this->handle, buf, 0x2);

		// Switch baudrate to 3Mbit
		printf("Switching baudrate...\n");
		memset(buf, 0x00, 0x40);
		buf[0] = 0x80;
		buf[1] = 0x03;
		hid_exchange(this->handle, buf, 0x2);

		//Do handshaking again at new baudrate so the firmware pulls pin 3 low?
		printf("Doing handshake...\n");
		memset(buf, 0x00, 0x40);
		buf[0] = 0x80;
		buf[1] = 0x02;
		hid_exchange(this->handle, buf, 0x2);

		//Only talk HID from now on
		printf("Only talk HID...\n");
		memset(buf, 0x00, 0x40);
		buf[0] = 0x80;
		buf[1] = 0x04;
		hid_exchange(this->handle, buf, 0x2);

		// Enable vibration
		printf("Enabling vibration...\n");
		memset(buf, 0x00, 0x400);
		buf[0] = 0x01; // Enabled
		send_subcommand(0x1, 0x48, buf, 1);

		// Enable IMU data
		printf("Enabling IMU data...\n");
		memset(buf, 0x00, 0x400);
		buf[0] = 0x01; // Enabled
		send_subcommand(0x1, 0x40, buf, 1);

		printf("Successfully initialized %s!\n", this->name.c_str());
	}

	void deinit_usb() {
		unsigned char buf[0x40];
		memset(buf, 0x00, 0x40);

		//Let the Joy-Con talk BT again    
		buf[0] = 0x80;
		buf[1] = 0x05;
		hid_exchange(this->handle, buf, 0x2);
		printf("Deinitialized %s\n", this->name.c_str());
	}


	// calibrated sticks:
	// Credit to Hypersect (Ryan Juckett)
	// http://blog.hypersect.com/interpreting-analog-sticks/
	void CalcAnalogStick() {

		if (this->left_right == 1) {
			CalcAnalogStick2(
				&this->stick.CalX,
				&this->stick.CalY,
				this->stick.x,
				this->stick.y,
				this->stick_cal_x_l,
				this->stick_cal_y_l);

		} else if (this->left_right == 2) {
			CalcAnalogStick2(
				&this->stick.CalX,
				&this->stick.CalY,
				this->stick2.x,
				this->stick2.y,
				this->stick_cal_x_r,
				this->stick_cal_y_r);

		} else if (this->left_right == 3) {
			CalcAnalogStick2(
				&this->stick.CalX,
				&this->stick.CalY,
				this->stick.x,
				this->stick.y,
				this->stick_cal_x_l,
				this->stick_cal_y_l);

			CalcAnalogStick2(
				&this->stick2.CalX,
				&this->stick2.CalY,
				this->stick2.x,
				this->stick2.y,
				this->stick_cal_x_r,
				this->stick_cal_y_r);
		}
	}


	void CalcAnalogStick2
	(
		float *pOutX,       // out: resulting stick X value
		float *pOutY,       // out: resulting stick Y value
		uint16_t x,              // in: initial stick X value
		uint16_t y,              // in: initial stick Y value
		uint16_t x_calc[3],      // calc -X, CenterX, +X
		uint16_t y_calc[3]       // calc -Y, CenterY, +Y
	)
	{

		float x_f, y_f;
		// Apply Joy-Con center deadzone. 0xAE translates approx to 15%. Pro controller has a 10% () deadzone
		float deadZoneCenter = 0.15f;
		// Add a small ammount of outer deadzone to avoid edge cases or machine variety.
		float deadZoneOuter = 0.10f;

		// convert to float based on calibration and valid ranges per +/-axis
		x = clamp(x, x_calc[0], x_calc[2]);
		y = clamp(y, y_calc[0], y_calc[2]);
		if (x >= x_calc[1]) {
			x_f = (float)(x - x_calc[1]) / (float)(x_calc[2] - x_calc[1]);
		} else {
			x_f = -((float)(x - x_calc[1]) / (float)(x_calc[0] - x_calc[1]));
		}
		if (y >= y_calc[1]) {
			y_f = (float)(y - y_calc[1]) / (float)(y_calc[2] - y_calc[1]);
		} else {
			y_f = -((float)(y - y_calc[1]) / (float)(y_calc[0] - y_calc[1]));
		}

		// Interpolate zone between deadzones
		float mag = sqrtf(x_f*x_f + y_f*y_f);
		if (mag > deadZoneCenter) {
			// scale such that output magnitude is in the range [0.0f, 1.0f]
			float legalRange = 1.0f - deadZoneOuter - deadZoneCenter;
			float normalizedMag = min(1.0f, (mag - deadZoneCenter) / legalRange);
			float scale = normalizedMag / mag;
			pOutX[1] = x_f * scale;
			pOutY[1] = y_f * scale;
		} else {
			// stick is in the inner dead zone
			pOutX[1] = 0.0f;
			pOutY[1] = 0.0f;
		}
	}

	// SPI (CTCaer):

	int get_spi_data(uint32_t offset, const uint16_t read_len, uint8_t *test_buf) {
		int res;
		uint8_t buf[0x100];
		while (1) {
			memset(buf, 0, sizeof(buf));
			auto hdr = (brcm_hdr *)buf;
			auto pkt = (brcm_cmd_01 *)(hdr + 1);
			hdr->cmd = 1;
			hdr->rumble[0] = timing_byte;

			buf[1] = timing_byte;

			timing_byte++;
			if (timing_byte > 0xF) {
				timing_byte = 0x0;
			}
			pkt->subcmd = 0x10;
			pkt->offset = offset;
			pkt->size = read_len;

			for (int i = 11; i < 22; ++i) {
				buf[i] = buf[i+3];
			}

			//hex_dump(buf, 20);

			res = hid_write(handle, buf, sizeof(*hdr) + sizeof(*pkt));

			res = hid_read(handle, buf, sizeof(buf));

			if ((*(uint16_t*)&buf[0xD] == 0x1090) && (*(uint32_t*)&buf[0xF] == offset)) {
				break;
			}
		}
		if (res >= 0x14 + read_len) {
			for (int i = 0; i < read_len; i++) {
				test_buf[i] = buf[0x14 + i];
			}
		}

		return 0;
	}

	int write_spi_data(uint32_t offset, const uint16_t write_len, uint8_t* test_buf) {
		int res;
		uint8_t buf[0x100];
		int error_writing = 0;
		while (1) {
			memset(buf, 0, sizeof(buf));
			auto hdr = (brcm_hdr *)buf;
			auto pkt = (brcm_cmd_01 *)(hdr + 1);
			hdr->cmd = 1;
			hdr->rumble[0] = timing_byte;
			timing_byte++;
			if (timing_byte > 0xF) {
				timing_byte = 0x0;
			}
			pkt->subcmd = 0x11;
			pkt->offset = offset;
			pkt->size = write_len;
			for (int i = 0; i < write_len; i++) {
				buf[0x10 + i] = test_buf[i];
			}
			res = hid_write(handle, buf, sizeof(*hdr) + sizeof(*pkt) + write_len);

			res = hid_read(handle, buf, sizeof(buf));

			if (*(uint16_t*)&buf[0xD] == 0x1180)
				break;

			error_writing++;
			if (error_writing == 125) {
				return 1;
			}
		}

		return 0;

	}
};