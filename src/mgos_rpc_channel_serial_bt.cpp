#include "mgos_mongoose.h"

#include "mg_rpc.h"
#include "mgos_rpc.h"

#include "mgos_debug.h"
#include "mgos_sys_config.h"
#include "mgos_utils.h"

#include "common/cs_crc32.h"
#include "common/cs_dbg.h"
#include "common/mbuf.h"
#include "common/str_util.h"

#include "bluetooth_serial.h"

#define EOF_CHAR "\x04"

/* Old delimiter */
#define FRAME_DELIM_1 "\"\"\""
#define FRAME_DELIM_1_LEN 3
/* New delimiter (as of 2018/06/14). */
#define FRAME_DELIM_2 "\n"
#define FRAME_DELIM_2_LEN 1

namespace
{
   
    bool is_empty_frame(const struct mg_str f) 
    {
        for (size_t i = 0; i < f.len; i++) {
            if (!isspace((int) f.p[i])) return false;
        }
        return true;
    }

   
}//namespace

extern "C" 
{

    struct mg_rpc_channel_serial_bt_data 
    {
        BluetoothSerial* BT;
        bool connected;
        bool sending;
        bool sending_user_frame;
        bool delim_1_used;
        bool crc_used;
        struct mbuf recv_mbuf;
        struct mbuf send_mbuf;
    };

    static size_t serial_bt_read_channel(struct mg_rpc_channel_serial_bt_data *chd) 
    {
        int len = chd->BT->available();
        if (!len) return 0;

        struct mbuf *mb = &chd->recv_mbuf;
        size_t nr = 0;
        
        size_t free_bytes = mb->size - mb->len;
        if (free_bytes < len) 
            mbuf_resize(mb, mb->len + len);

        nr = chd->BT->read(mb->buf + mb->len, len);
        mb->len += nr;
        
        return nr;
    } 

    IRAM void mg_serial_bt_schedule_dispatcher() 
    {
    #ifdef MGOS_BOOT_BUILD
        mongoose_schedule_poll(false/*from_isr*/);
    #endif
        return;
    }

    static void mg_rpc_serial_bt_dispatcher(void* p)
    {
        struct mg_rpc_channel *ch = (struct mg_rpc_channel *)p;
        
        struct mg_rpc_channel_serial_bt_data *chd = 
            (struct mg_rpc_channel_serial_bt_data *) ch->channel_data;

       
        if (serial_bt_read_channel(chd))
        {
            size_t flen = 0;
            const char *end   = NULL;
            struct mbuf *rxb = &chd->recv_mbuf;
            
            while (true) 
            { 
                end = c_strnstr(rxb->buf, FRAME_DELIM_1, rxb->len);
                if (end != NULL) 
                {
                    chd->delim_1_used = true; /* Turn on compat mode. */
                    flen = end - rxb->buf;
                    end += FRAME_DELIM_1_LEN;
                }
                if (end == NULL) 
                {
                    end = c_strnstr(rxb->buf, FRAME_DELIM_2, rxb->len);
                    if (end != NULL) 
                    {
                        flen = end - rxb->buf;
                        end += FRAME_DELIM_2_LEN;
                    } 
                    else { break; }
                }

                struct mg_str f = mg_mk_str_n((const char *) rxb->buf, flen);
                if (!is_empty_frame(f)) 
                {
                    /*
                    * EOF_CHAR is used to turn off interactive console. If the frame is
                    * just EOF_CHAR by itself, we'll immediately send a frame containing
                    * eof_char in response (since the frame isn't valid anyway);
                    * otherwise we'll handle the frame.
                    */
                    if (mg_vcmp(&f, EOF_CHAR) == 0) 
                    {
                        mbuf_append(&chd->send_mbuf, FRAME_DELIM_1, FRAME_DELIM_1_LEN);
                        mbuf_append(&chd->send_mbuf, EOF_CHAR, 1);
                        mbuf_append(&chd->send_mbuf, FRAME_DELIM_1, FRAME_DELIM_1_LEN);
                        chd->sending = true;
                    } 
                    else 
                    {
                        /* Skip junk in front. */
                        while (f.len > 0 && *f.p != '{') {
                            f.len--;
                            f.p++;
                        }
                         /*
                        * Frame may be followed by metadata, which is a comma-separated
                        * list of values. Right now, only one field is expected:
                        * CRC32 checksum as a hex number.
                        */
                        struct mg_str meta = mg_mk_str_n(f.p + f.len, 0);
                        while (meta.p > f.p) {
                            if (*(meta.p - 1) == '}') break;
                            meta.p--;
                            f.len--;
                            meta.len++;
                        }
                        if (meta.len >= 8) {
                            ((char *) meta.p)[meta.len] =
                                '\0'; /* Stomps first char of the delimiter. */
                            uint32_t crc = cs_crc32(0, f.p, f.len);
                            uint32_t expected_crc = 0;
                            if (sscanf(meta.p, "%x", (int *) &expected_crc) != 1 ||
                                crc != expected_crc) {
                            LOG(LL_WARN,
                                ("%p Corrupted frame (%d): '%.*s' '%.*s' %08x %08x", ch,
                                (int) f.len, (int) f.len, f.p, (int) meta.len, meta.p,
                                (unsigned int) expected_crc, (unsigned int) crc));
                            f.len = 0;
                            } else {
                            chd->crc_used = true;
                            }
                        }

                        if (f.len > 0) 
                        {
                            ch->ev_handler(ch, MG_RPC_CHANNEL_FRAME_RECD, &f);
                        }
                    }
                }
                else 
                {
                    /* Respond with an empty frame to an empty frame */
                    mbuf_append(&chd->send_mbuf, FRAME_DELIM_2, FRAME_DELIM_2_LEN);
                }
                mbuf_remove(rxb, (end - rxb->buf));
            }//while (true)

            if ((int) rxb->len > mgos_sys_config_get_rpc_max_frame_size() + 2 * FRAME_DELIM_1_LEN) 
            {
                LOG(LL_ERROR, ("Incoming frame is too big, dropping."));
                mbuf_remove(rxb, rxb->len);
            }
            mbuf_trim(rxb);
        }

        if (chd->sending) 
        {
            int len = (int)MIN(chd->send_mbuf.len, 256);
 
            len = chd->BT->write(chd->send_mbuf.buf, len);
            mbuf_remove(&chd->send_mbuf, len);
            
            if (chd->send_mbuf.len == 0) 
            { 
                chd->sending = false;
                if (chd->sending_user_frame) 
                {
                    chd->sending_user_frame = false;
                    ch->ev_handler(ch, MG_RPC_CHANNEL_FRAME_SENT, (void*) 1);
                }
                mbuf_trim(&chd->send_mbuf);
            }
        }
    }

    static void mg_rpc_channel_serial_bt_ch_connect(struct mg_rpc_channel *ch) 
    {
         struct mg_rpc_channel_serial_bt_data *chd =
            (struct mg_rpc_channel_serial_bt_data *) ch->channel_data;

        if (!chd->connected) 
        {
        #ifndef MGOS_BOOT_BUILD
            mgos_add_poll_cb(mg_rpc_serial_bt_dispatcher, (void*)ch);
        #endif
            mg_serial_bt_schedule_dispatcher();
            chd->connected = true;
            ch->ev_handler(ch, MG_RPC_CHANNEL_OPEN, NULL);
        }
    }

    static void mg_rpc_channel_serial_bt_ch_destroy(struct mg_rpc_channel *ch) 
    {
        struct mg_rpc_channel_serial_bt_data *chd =
            (struct mg_rpc_channel_serial_bt_data *) ch->channel_data;
        mbuf_free(&chd->recv_mbuf);
        mbuf_free(&chd->send_mbuf);
        free(chd);
        free(ch);
    }

    static bool mg_rpc_channel_serial_bt_send_frame(struct mg_rpc_channel *ch, const struct mg_str f) 
    {
        struct mg_rpc_channel_serial_bt_data *chd =
            (struct mg_rpc_channel_serial_bt_data *) ch->channel_data;

        if (!chd->connected || chd->sending) return false;
        
        mbuf_append(&chd->send_mbuf, FRAME_DELIM_2, FRAME_DELIM_2_LEN);
        if (chd->delim_1_used) {
            mbuf_append(&chd->send_mbuf, FRAME_DELIM_1, FRAME_DELIM_1_LEN);
        }
        mbuf_append(&chd->send_mbuf, f.p, f.len);
        if (chd->crc_used) {
            char crc_hex[9];
            sprintf(crc_hex, "%08x", (unsigned int) cs_crc32(0, f.p, f.len));
            mbuf_append(&chd->send_mbuf, crc_hex, 8);
        }
        if (chd->delim_1_used) {
            mbuf_append(&chd->send_mbuf, FRAME_DELIM_1, FRAME_DELIM_1_LEN);
        }
        mbuf_append(&chd->send_mbuf, FRAME_DELIM_2, FRAME_DELIM_2_LEN);
        chd->sending = chd->sending_user_frame = true;
        
        mg_serial_bt_schedule_dispatcher();
        return true;
    }

    static void mg_rpc_channel_serial_bt_ch_close(struct mg_rpc_channel *ch) 
    {
        struct mg_rpc_channel_serial_bt_data *chd =
                (struct mg_rpc_channel_serial_bt_data *) ch->channel_data;
        
        mgos_remove_poll_cb(mg_rpc_serial_bt_dispatcher, (void*)(ch));
        chd->connected = chd->sending = chd->sending_user_frame = false;
        chd->BT->end();
        ch->ev_handler(ch, MG_RPC_CHANNEL_CLOSED, NULL);
    }

    static const char *mg_rpc_channel_serial_bt_get_type(struct mg_rpc_channel *ch) 
    {
        return "SerialBT";
    }

    static bool mg_rpc_channel_serial_bt_get_authn_info(struct mg_rpc_channel *ch, const char *auth_domain, 
        const char *auth_file, struct mg_rpc_authn_info *authn) 
    {
        (void) ch;
        (void) auth_domain;
        (void) auth_file;
        (void) authn;

        return false;
    }

    static char *mg_rpc_channel_serial_bt_get_info(struct mg_rpc_channel *ch) 
    {
        char *res = NULL;
        //mg_asprintf(&res, 0, "%s", "No channel info.");
        return res;
    }

    struct mg_rpc_channel *mg_rpc_channel_serial_bt(BluetoothSerial* bt)
    {
        struct mg_rpc_channel *ch = (struct mg_rpc_channel *) calloc(1, sizeof(*ch));
        ch->ch_connect = mg_rpc_channel_serial_bt_ch_connect;
        ch->send_frame = mg_rpc_channel_serial_bt_send_frame;
        ch->ch_close = mg_rpc_channel_serial_bt_ch_close;
        ch->ch_destroy = mg_rpc_channel_serial_bt_ch_destroy;
        ch->get_type = mg_rpc_channel_serial_bt_get_type;
        ch->is_persistent = mg_rpc_channel_true;
        ch->is_broadcast_enabled = mg_rpc_channel_true;
        ch->get_authn_info = mg_rpc_channel_serial_bt_get_authn_info;
        ch->get_info = mg_rpc_channel_serial_bt_get_info;
        struct mg_rpc_channel_serial_bt_data *chd =
            (struct mg_rpc_channel_serial_bt_data *) calloc(1, sizeof(*chd));
        mbuf_init(&chd->recv_mbuf, 0);
        mbuf_init(&chd->send_mbuf, 0);
        chd->connected = chd->sending = chd->sending_user_frame =
            chd->delim_1_used = chd->crc_used = false;
        chd->BT = bt;
        ch->channel_data = chd;
        return ch;
    }

    bool mgos_rpc_serial_bt_init(BluetoothSerial* bt) 
    {
        if (mgos_rpc_get_global() == NULL) 
            return true;

        // create channel
        struct mg_rpc_channel* ch = mg_rpc_channel_serial_bt(bt);
        if (ch == NULL) {
            return false;
        }

        //cs_log_set_level(LL_DEBUG);

        // add channel to global rpc channels list
        mg_rpc_add_channel(mgos_rpc_get_global(), mg_mk_str("sbt"), ch);
        
        ch->ch_connect(ch);
        return true;
    }


}//extern "C"
