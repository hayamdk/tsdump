static const uint32_t crc32tab[256] = {
	0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
	0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
	0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
	0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
	0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
	0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
	0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
	0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
	0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
	0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
	0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
	0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
	0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
	0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
	0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
	0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
	0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
	0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
	0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
	0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
	0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
	0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
	0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
	0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
	0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
	0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
	0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
	0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
	0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
	0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
	0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
	0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
	0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
	0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
	0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
	0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
	0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
	0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
	0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
	0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
	0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
	0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
	0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
	0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
	0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
	0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
	0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
	0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
	0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
	0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
	0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
	0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
	0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
	0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
	0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
	0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
	0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
	0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
	0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
	0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4,
};

static inline uint32_t crc32(unsigned char *buf, int len)
{
	uint32_t crc = 0xffffffff;
	for (int i = 0; i<len; i++){
		crc = (crc << 8)
			^ crc32tab[((crc >> 24) ^ buf[i]) & 0xff];
	}
	return crc;
}

typedef struct{
	uint8_t sync_byte;
	int valid_sync_byte;
	unsigned int transport_error_indicator : 1;
	unsigned int payload_unit_start_indicator : 1;
	unsigned int transport_priority : 1;
	unsigned int pid : 13;
	unsigned int transport_scrambling_control : 2;
	unsigned int adaptation_field_control : 2;
	unsigned int continuity_counter : 4;
	uint8_t adaptation_field_len;
	uint8_t pointer_field;
	unsigned int section_length : 12;

	uint8_t adaptation_field_pos;
	uint8_t payload_pos;
	uint8_t payload_data_pos;
} ts_header_t;

int parse_ts_header(const uint8_t *packet, ts_header_t *tsh);

static inline unsigned int get_bits(const uint8_t *buf, size_t offset, size_t length)
{
	unsigned int t = 0;
	size_t offset_bytes, offset_bits, len1, len2, len3, i;

	if (length == 0) {
		return 0;
	}

	offset_bytes = offset / 8;
	offset_bits = offset - offset_bytes * 8;
	len1 = 8 - offset_bits;
	if (len1 > length) {
		len1 = length;
	}
	len2 = (length - len1) / 8;
	len3 = length - len1 - len2 * 8;
	t = (buf[offset_bytes] & (0xff>>offset_bits)) >> (8-len1-offset_bits);
	for (i = 1; i <= len2; i++) {
		t <<= 8;
		t += buf[offset_bytes + i];
	}
	t <<= len3;
	t += buf[offset_bytes + len2 + 1] >> (8 - len3);
	return t;
}

static inline uint64_t get_bits64(const uint8_t *buf, size_t offset, size_t length)
{
	uint64_t t;
	size_t length2;
	if (length > 32) {
		length2 = length - 32;
		t = get_bits(buf, offset, 32);
		t = t << length2;
		t |= get_bits(buf, offset + 32, length2);
	} else {
		t = get_bits(buf, offset, length);
	}
	return t;
}

static inline int ts_get_section_length(const uint8_t *p, const ts_header_t *tsh)
{
	int pos = tsh->payload_data_pos;
	if (pos >= 188 - 3) {
		return -1;
	}
	return get_bits(p, pos * 8 + 12, 12);
}

static inline uint32_t get_payload_crc32(PSI_parse_t *ps)
{
	return
		ps->payload[ps->n_payload - 4] * 0x1000000 +
		ps->payload[ps->n_payload - 3] * 0x10000 +
		ps->payload[ps->n_payload - 2] * 0x100 +
		ps->payload[ps->n_payload - 1];
}

typedef struct {
	PSI_parse_t pid0x00;
	PSI_parse_t pid0x11;
	PSI_parse_t pid0x12;
	PSI_parse_t pid0x14;
	PSI_parse_t pid0x26;
	PSI_parse_t pid0x27;
	int n_services;
	proginfo_t proginfos[MAX_SERVICES_PER_CH];
	PSI_parse_t PMT_payloads[MAX_SERVICES_PER_CH];
} ts_service_list_t;

/* 短形式イベント記述子（Short event descriptor） */
typedef struct {
	//unsigned int descriptor_tag : 8;
	unsigned int descriptor_length : 8;
	char ISO_639_language_code[4];
	unsigned int event_name_length : 8;
	const uint8_t *event_name_char;
	unsigned int text_length : 8;
	const uint8_t *text_char;
} Sed_t;

/* 拡張形式イベント記述子（Extended event descriptor） */
typedef struct {
	//unsigned int descriptor_tag : 8;
	unsigned int descriptor_length : 8;
	unsigned int descriptor_number : 4;
	unsigned int last_descriptor_number : 4;
	char ISO_639_language_code[4];
	unsigned int length_of_items : 8;

	unsigned int text_length : 8;
	const uint8_t *text_char;
} Eed_t;

typedef struct {
	unsigned int item_description_length : 8;
	const uint8_t *item_description_char;
	unsigned int item_length : 8;
	const uint8_t *item_char;
} Eed_item_t;

typedef struct {
	unsigned int event_id : 16;
	unsigned int start_time_mjd : 16;
	unsigned int start_time_jtc : 24;
	unsigned int duration : 24;
	unsigned int running_status : 3;
	unsigned int free_CA_mode : 1;
	unsigned int descriptors_loop_length : 12;
} EIT_body_t;

typedef struct {
	unsigned int table_id : 8;
	unsigned int section_syntax_indicator : 1;
	unsigned int section_length : 12;
	unsigned int service_id : 16;
	unsigned int version_number : 5;
	unsigned int current_next_indicator : 1;
	unsigned int section_number : 8;
	unsigned int last_section_number : 8;
	unsigned int transport_stream_id : 16;
	unsigned int original_network_id : 16;
	unsigned int segment_last_section_number : 8;
	unsigned int last_table_id : 8;
} EIT_header_t;

/* サービス記述子（Service descriptor） */
typedef struct {
	//unsigned int descriptor_tag : 8;
	unsigned int descriptor_length : 8;
	unsigned int service_type : 8;
	unsigned int service_provider_name_length : 8;
	const uint8_t *service_provider_name_char;
	unsigned int service_name_length : 8;
	const uint8_t *service_name_char;
} Sd_t;

typedef struct {
	unsigned int service_id : 16;
	unsigned int EIT_user_defined_flags : 3;
	unsigned int EIT_schedule_flag : 1;
	unsigned int EIT_present_following_flag : 1;
	unsigned int running_status : 3;
	unsigned int free_CA_mode : 1;
	unsigned int descriptors_loop_length : 12;
} SDT_body_t;

typedef struct {
	unsigned int table_id : 8;
	unsigned int section_syntax_indicator : 1;
	unsigned int section_length : 12;
	unsigned int transport_stream_id : 16;
	unsigned int version_number : 5;
	unsigned int current_next_indicator : 1;
	unsigned int section_number : 8;
	unsigned int last_section_number : 8;
	unsigned int original_network_id : 16;
} SDT_header_t;

typedef struct {
	unsigned int program_number : 16;
	unsigned int pid : 13;
} PAT_item_t;

typedef proginfo_t* (*eit_callback_handler_t)(void*, const EIT_header_t*);
typedef proginfo_t* (*service_callback_handler_t)(void*, const unsigned int);
typedef void (*pat_callback_handler_t)(void*, const int, const int, const PAT_item_t*);
typedef void(*tot_callback_handler_t)(void*, const time_mjd_t*);
void store_TOT(proginfo_t *proginfo, const time_mjd_t *TOT_time);
void store_PAT(proginfo_t *proginfo, const PAT_item_t *PAT_item);

void parse_EIT(PSI_parse_t *payload_stat, const uint8_t *packet, const ts_header_t *tsh, void *param, eit_callback_handler_t handler);
void parse_SDT(PSI_parse_t *payload_stat, const uint8_t *packet, const ts_header_t *tsh, void *param, service_callback_handler_t handler);
void parse_PAT(PSI_parse_t *PAT_payload, const uint8_t *packet, const ts_header_t *tsh, void *param, pat_callback_handler_t handler);
void parse_PMT(const uint8_t *packet, const ts_header_t *tsh, PSI_parse_t *PMT_payload, proginfo_t *proginfo);
void parse_PCR(const uint8_t *packet, const ts_header_t *tsh, void *param, service_callback_handler_t handler);
void parse_TOT_TDT(const uint8_t *packet, const ts_header_t *tsh, PSI_parse_t *TOT_payload, void *param, tot_callback_handler_t handler);

void clear_proginfo_all(proginfo_t *proginfo);
void init_proginfo(proginfo_t *proginfo);