/*
 * DTLS implementation written by Nagendra Modadugu
 * (nagendra@cs.stanford.edu) for the OpenSSL project 2005.
 */
/* ====================================================================
 * Copyright (c) 1999-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com). */

#include <openssl/base.h>

#include <stdio.h>

#if defined(OPENSSL_WINDOWS)
#include <sys/timeb.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include <openssl/err.h>
#include <openssl/mem.h>
#include <openssl/obj.h>

#include "ssl_locl.h"

static void get_current_time(OPENSSL_timeval *t);
static OPENSSL_timeval* dtls1_get_timeout(SSL *s, OPENSSL_timeval* timeleft);
static void dtls1_set_handshake_header(SSL *s, int type, unsigned long len);
static int dtls1_handshake_write(SSL *s, enum should_add_to_finished_hash should_add_to_finished_hash);
static void dtls1_add_to_finished_hash(SSL *s);

const SSL3_ENC_METHOD DTLSv1_enc_data = {
    	tls1_enc,
	tls1_mac,
	tls1_setup_key_block,
	tls1_generate_master_secret,
	tls1_change_cipher_state,
	tls1_final_finish_mac,
	TLS1_FINISH_MAC_LENGTH,
	tls1_cert_verify_mac,
	TLS_MD_CLIENT_FINISH_CONST,TLS_MD_CLIENT_FINISH_CONST_SIZE,
	TLS_MD_SERVER_FINISH_CONST,TLS_MD_SERVER_FINISH_CONST_SIZE,
	tls1_alert_code,
	tls1_export_keying_material,
	SSL_ENC_FLAG_DTLS|SSL_ENC_FLAG_EXPLICIT_IV,
	DTLS1_HM_HEADER_LENGTH,
	dtls1_set_handshake_header,
	dtls1_handshake_write,
	dtls1_add_to_finished_hash,
	};

const SSL3_ENC_METHOD DTLSv1_2_enc_data = {
    	tls1_enc,
	tls1_mac,
	tls1_setup_key_block,
	tls1_generate_master_secret,
	tls1_change_cipher_state,
	tls1_final_finish_mac,
	TLS1_FINISH_MAC_LENGTH,
	tls1_cert_verify_mac,
	TLS_MD_CLIENT_FINISH_CONST,TLS_MD_CLIENT_FINISH_CONST_SIZE,
	TLS_MD_SERVER_FINISH_CONST,TLS_MD_SERVER_FINISH_CONST_SIZE,
	tls1_alert_code,
	tls1_export_keying_material,
	SSL_ENC_FLAG_DTLS|SSL_ENC_FLAG_EXPLICIT_IV|SSL_ENC_FLAG_SIGALGS
		|SSL_ENC_FLAG_SHA256_PRF|SSL_ENC_FLAG_TLS1_2_CIPHERS,
	DTLS1_HM_HEADER_LENGTH,
	dtls1_set_handshake_header,
	dtls1_handshake_write,
	dtls1_add_to_finished_hash,
	};

int dtls1_new(SSL *s)
	{
	DTLS1_STATE *d1;

	if (!ssl3_new(s)) return(0);
	if ((d1=OPENSSL_malloc(sizeof *d1)) == NULL)
		{
		ssl3_free(s);
		return (0);
		}
	memset(d1,0, sizeof *d1);

	/* d1->handshake_epoch=0; */

	d1->unprocessed_rcds.q=pqueue_new();
	d1->processed_rcds.q=pqueue_new();
	d1->buffered_messages = pqueue_new();
	d1->sent_messages=pqueue_new();
	d1->buffered_app_data.q=pqueue_new();

	if ( s->server)
		{
		d1->cookie_len = sizeof(s->d1->cookie);
		}

	if( ! d1->unprocessed_rcds.q || ! d1->processed_rcds.q 
        || ! d1->buffered_messages || ! d1->sent_messages || ! d1->buffered_app_data.q)
		{
        if ( d1->unprocessed_rcds.q) pqueue_free(d1->unprocessed_rcds.q);
        if ( d1->processed_rcds.q) pqueue_free(d1->processed_rcds.q);
        if ( d1->buffered_messages) pqueue_free(d1->buffered_messages);
		if ( d1->sent_messages) pqueue_free(d1->sent_messages);
		if ( d1->buffered_app_data.q) pqueue_free(d1->buffered_app_data.q);
		OPENSSL_free(d1);
		ssl3_free(s);
		return (0);
		}

	s->d1=d1;
	s->method->ssl_clear(s);
	return(1);
	}

static void dtls1_clear_queues(SSL *s)
	{
    pitem *item = NULL;
    hm_fragment *frag = NULL;
	DTLS1_RECORD_DATA *rdata;

    while( (item = pqueue_pop(s->d1->unprocessed_rcds.q)) != NULL)
        {
		rdata = (DTLS1_RECORD_DATA *) item->data;
		if (rdata->rbuf.buf)
			{
			OPENSSL_free(rdata->rbuf.buf);
			}
        OPENSSL_free(item->data);
        pitem_free(item);
        }

    while( (item = pqueue_pop(s->d1->processed_rcds.q)) != NULL)
        {
		rdata = (DTLS1_RECORD_DATA *) item->data;
		if (rdata->rbuf.buf)
			{
			OPENSSL_free(rdata->rbuf.buf);
			}
        OPENSSL_free(item->data);
        pitem_free(item);
        }

    while( (item = pqueue_pop(s->d1->buffered_messages)) != NULL)
        {
        frag = (hm_fragment *)item->data;
        dtls1_hm_fragment_free(frag);
        pitem_free(item);
        }

    while ( (item = pqueue_pop(s->d1->sent_messages)) != NULL)
        {
        frag = (hm_fragment *)item->data;
        dtls1_hm_fragment_free(frag);
        pitem_free(item);
        }

	while ( (item = pqueue_pop(s->d1->buffered_app_data.q)) != NULL)
		{
		rdata = (DTLS1_RECORD_DATA *) item->data;
		if (rdata->rbuf.buf)
			{
			OPENSSL_free(rdata->rbuf.buf);
			}
		OPENSSL_free(item->data);
		pitem_free(item);
		}
	}

void dtls1_free(SSL *s)
	{
	ssl3_free(s);

	dtls1_clear_queues(s);

    pqueue_free(s->d1->unprocessed_rcds.q);
    pqueue_free(s->d1->processed_rcds.q);
    pqueue_free(s->d1->buffered_messages);
	pqueue_free(s->d1->sent_messages);
	pqueue_free(s->d1->buffered_app_data.q);

	OPENSSL_free(s->d1);
	s->d1 = NULL;
	}

void dtls1_clear(SSL *s)
	{
    pqueue unprocessed_rcds;
    pqueue processed_rcds;
    pqueue buffered_messages;
	pqueue sent_messages;
	pqueue buffered_app_data;
	unsigned int mtu;

	if (s->d1)
		{
		unprocessed_rcds = s->d1->unprocessed_rcds.q;
		processed_rcds = s->d1->processed_rcds.q;
		buffered_messages = s->d1->buffered_messages;
		sent_messages = s->d1->sent_messages;
		buffered_app_data = s->d1->buffered_app_data.q;
		mtu = s->d1->mtu;

		dtls1_clear_queues(s);

		memset(s->d1, 0, sizeof(*(s->d1)));

		if (SSL_get_options(s) & SSL_OP_NO_QUERY_MTU)
			{
			s->d1->mtu = mtu;
			}

		s->d1->unprocessed_rcds.q = unprocessed_rcds;
		s->d1->processed_rcds.q = processed_rcds;
		s->d1->buffered_messages = buffered_messages;
		s->d1->sent_messages = sent_messages;
		s->d1->buffered_app_data.q = buffered_app_data;
		}

	ssl3_clear(s);
	if (s->method->version == DTLS_ANY_VERSION)
		s->version=DTLS1_2_VERSION;
	else
		s->version=s->method->version;
	}

long dtls1_ctrl(SSL *s, int cmd, long larg, void *parg)
	{
	int ret=0;

	switch (cmd)
		{
	case DTLS_CTRL_GET_TIMEOUT:
		if (dtls1_get_timeout(s, (OPENSSL_timeval*) parg) != NULL)
			{
			ret = 1;
			}
		break;
	case DTLS_CTRL_HANDLE_TIMEOUT:
		ret = dtls1_handle_timeout(s);
		break;

	default:
		ret = ssl3_ctrl(s, cmd, larg, parg);
		break;
		}
	return(ret);
	}

/*
 * As it's impossible to use stream ciphers in "datagram" mode, this
 * simple filter is designed to disengage them in DTLS. Unfortunately
 * there is no universal way to identify stream SSL_CIPHER, so we have
 * to explicitly list their SSL_* codes. Currently RC4 is the only one
 * available, but if new ones emerge, they will have to be added...
 */
const SSL_CIPHER *dtls1_get_cipher(unsigned int u)
	{
	const SSL_CIPHER *ciph = ssl3_get_cipher(u);

	if (ciph != NULL)
		{
		if (ciph->algorithm_enc == SSL_RC4)
			return NULL;
		/* TODO(davidben): EVP_AEAD does not work in DTLS yet. */
		if (ciph->algorithm2 & SSL_CIPHER_ALGORITHM2_AEAD ||
			ciph->algorithm2 & SSL_CIPHER_ALGORITHM2_STATEFUL_AEAD)
			return NULL;
		}

	return ciph;
	}

void dtls1_start_timer(SSL *s)
	{
	/* If timer is not set, initialize duration with 1 second */
	if (s->d1->next_timeout.tv_sec == 0 && s->d1->next_timeout.tv_usec == 0)
		{
		s->d1->timeout_duration = 1;
		}
	
	/* Set timeout to current time */
	get_current_time(&s->d1->next_timeout);

	/* Add duration to current time */
	s->d1->next_timeout.tv_sec += s->d1->timeout_duration;
	BIO_ctrl(SSL_get_rbio(s), BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT, 0, &s->d1->next_timeout);
	}

static OPENSSL_timeval* dtls1_get_timeout(SSL *s, OPENSSL_timeval* timeleft)
	{
	OPENSSL_timeval timenow;

	/* If no timeout is set, just return NULL */
	if (s->d1->next_timeout.tv_sec == 0 && s->d1->next_timeout.tv_usec == 0)
		{
		return NULL;
		}

	/* Get current time */
	get_current_time(&timenow);

	/* If timer already expired, set remaining time to 0 */
	if (s->d1->next_timeout.tv_sec < timenow.tv_sec ||
		(s->d1->next_timeout.tv_sec == timenow.tv_sec &&
		 s->d1->next_timeout.tv_usec <= timenow.tv_usec))
		{
		memset(timeleft, 0, sizeof(OPENSSL_timeval));
		return timeleft;
		}

	/* Calculate time left until timer expires */
	memcpy(timeleft, &s->d1->next_timeout, sizeof(OPENSSL_timeval));
	timeleft->tv_sec -= timenow.tv_sec;
	timeleft->tv_usec -= timenow.tv_usec;
	if (timeleft->tv_usec < 0)
		{
		timeleft->tv_sec--;
		timeleft->tv_usec += 1000000;
		}

	/* If remaining time is less than 15 ms, set it to 0
	 * to prevent issues because of small devergences with
	 * socket timeouts.
	 */
	if (timeleft->tv_sec == 0 && timeleft->tv_usec < 15000)
		{
		memset(timeleft, 0, sizeof(OPENSSL_timeval));
		}
	

	return timeleft;
	}

int dtls1_is_timer_expired(SSL *s)
	{
	OPENSSL_timeval timeleft;

	/* Get time left until timeout, return false if no timer running */
	if (dtls1_get_timeout(s, &timeleft) == NULL)
		{
		return 0;
		}

	/* Return false if timer is not expired yet */
	if (timeleft.tv_sec > 0 || timeleft.tv_usec > 0)
		{
		return 0;
		}

	/* Timer expired, so return true */	
	return 1;
	}

void dtls1_double_timeout(SSL *s)
	{
	s->d1->timeout_duration *= 2;
	if (s->d1->timeout_duration > 60)
		s->d1->timeout_duration = 60;
	dtls1_start_timer(s);
	}

void dtls1_stop_timer(SSL *s)
	{
	/* Reset everything */
	memset(&(s->d1->timeout), 0, sizeof(struct dtls1_timeout_st));
	memset(&s->d1->next_timeout, 0, sizeof(OPENSSL_timeval));
	s->d1->timeout_duration = 1;
	BIO_ctrl(SSL_get_rbio(s), BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT, 0, &s->d1->next_timeout);
	/* Clear retransmission buffer */
	dtls1_clear_record_buffer(s);
	}

int dtls1_check_timeout_num(SSL *s)
	{
	s->d1->timeout.num_alerts++;

	/* Reduce MTU after 2 unsuccessful retransmissions */
	if (s->d1->timeout.num_alerts > 2)
		{
		s->d1->mtu = BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_GET_FALLBACK_MTU, 0, NULL);		
		}

	if (s->d1->timeout.num_alerts > DTLS1_TMO_ALERT_COUNT)
		{
		/* fail the connection, enough alerts have been sent */
		OPENSSL_PUT_ERROR(SSL, dtls1_check_timeout_num, SSL_R_READ_TIMEOUT_EXPIRED);
		return -1;
		}

	return 0;
	}

int dtls1_handle_timeout(SSL *s)
	{
	/* if no timer is expired, don't do anything */
	if (!dtls1_is_timer_expired(s))
		{
		return 0;
		}

	dtls1_double_timeout(s);

	if (dtls1_check_timeout_num(s) < 0)
		return -1;

	s->d1->timeout.read_timeouts++;
	if (s->d1->timeout.read_timeouts > DTLS1_TMO_READ_COUNT)
		{
		s->d1->timeout.read_timeouts = 1;
		}

	dtls1_start_timer(s);
	return dtls1_retransmit_buffered_messages(s);
	}

static void get_current_time(OPENSSL_timeval *t)
{
#if defined(OPENSSL_WINDOWS)
	struct _timeb time;
	_ftime(&time);
	t->tv_sec = time.time;
	t->tv_usec = time.millitm * 1000;
#else
	gettimeofday(t, NULL);
#endif
}

static void dtls1_set_handshake_header(SSL *s, int htype, unsigned long len)
	{
	unsigned char *p = (unsigned char *)s->init_buf->data;
	dtls1_set_message_header(s, p, htype, len, 0, len);
	s->init_num = (int)len + DTLS1_HM_HEADER_LENGTH;
	s->init_off = 0;
	/* Buffer the message to handle re-xmits */
	dtls1_buffer_message(s, 0);
	}

static int dtls1_handshake_write(SSL *s, enum should_add_to_finished_hash should_add_to_finished_hash)
	{
	return dtls1_do_write(s, SSL3_RT_HANDSHAKE, should_add_to_finished_hash);
	}

static void dtls1_add_to_finished_hash(SSL *s)
	{
	uint8_t *record = (uint8_t *) &s->init_buf->data[s->init_off];
	const struct hm_header_st *msg_hdr = &s->d1->w_msg_hdr;
	uint8_t serialised_header[DTLS1_HM_HEADER_LENGTH];
	uint8_t *p = serialised_header;

	/* Construct the message header as if it were a single fragment. */
	*p++ = msg_hdr->type;
	l2n3(msg_hdr->msg_len, p);
	s2n (msg_hdr->seq, p);
	l2n3(0, p);
	l2n3(msg_hdr->msg_len, p);
	ssl3_finish_mac(s, serialised_header, sizeof(serialised_header));
	ssl3_finish_mac(s, record + DTLS1_HM_HEADER_LENGTH,
			s->init_num - DTLS1_HM_HEADER_LENGTH);
	}
