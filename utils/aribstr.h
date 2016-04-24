#ifndef ARIBSTR_H
#define ARIBSTR_H 1

#ifdef __cplusplus
extern "C"{
#endif /* __cplusplus */

	int AribToString(TSDCHAR *dst, const int dst_maxlen, const uint8_t *src, const int src_len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
