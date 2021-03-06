/*
 * Copyright (C) 2019,2020 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "sbi-path.h"

static int server_cb(ogs_sbi_server_t *server,
        ogs_sbi_session_t *session, ogs_sbi_request_t *request)
{
    ausf_event_t *e = NULL;
    int rv;

    ogs_assert(session);
    ogs_assert(request);

    e = ausf_event_new(AUSF_EVT_SBI_SERVER);
    ogs_assert(e);

    e->sbi.server = server;
    e->sbi.session = session;
    e->sbi.request = request;

    rv = ogs_queue_push(ogs_app()->queue, e);
    if (rv != OGS_OK) {
        ogs_warn("ogs_queue_push() failed:%d", (int)rv);
        ausf_event_free(e);
        return OGS_ERROR;
    }

    return OGS_OK;
}

static int client_cb(ogs_sbi_response_t *response, void *data)
{
    ausf_event_t *e = NULL;
    int rv;

    ogs_assert(response);

    e = ausf_event_new(AUSF_EVT_SBI_CLIENT);
    ogs_assert(e);
    e->sbi.response = response;
    e->sbi.data = data;

    rv = ogs_queue_push(ogs_app()->queue, e);
    if (rv != OGS_OK) {
        ogs_warn("ogs_queue_push() failed:%d", (int)rv);
        ausf_event_free(e);
        return OGS_ERROR;
    }

    return OGS_OK;
}

int ausf_sbi_open(void)
{
    ogs_sbi_nf_instance_t *nf_instance = NULL;

    ogs_sbi_server_start_all(server_cb);

    /*
     * The connection between NF and NRF is a little special.
     *
     * NF and NRF share nf_instance. I get the NRF EndPoint(client) information
     * the configuration file via lib/sbi/context.c.
     * And, the NFService information will be transmitted to NRF.
     *
     * ogs_sbi_self()->nf_instance_id means NF's InstanceId.
     */
    ogs_list_for_each(&ogs_sbi_self()->nf_instance_list, nf_instance) {
        ogs_sbi_nf_service_t *service = NULL;
        ogs_sbi_client_t *client = NULL;

        /* Build NF instance information. It will be transmitted to NRF. */
        ogs_sbi_nf_instance_build_default(nf_instance, ausf_self()->nf_type);

        /* Build NF service information. It will be transmitted to NRF. */
        service = ogs_sbi_nf_service_build_default(nf_instance,
                (char*)OGS_SBI_SERVICE_NAME_NAUSF_AUTH);
        ogs_assert(service);
        ogs_sbi_nf_service_add_version(service, (char*)OGS_SBI_API_V1,
                (char*)OGS_SBI_API_V1_0_0, NULL);

        /* Client callback is only used when NF sends to NRF */
        client = nf_instance->client;
        ogs_assert(client);
        client->cb = client_cb;

        /* NFRegister is sent and the response is received
         * by the above client callback. */
        ausf_nf_fsm_init(nf_instance);
    }

    return OGS_OK;
}

void ausf_sbi_close(void)
{
    ogs_sbi_server_stop_all();
}

void ausf_sbi_send(ogs_sbi_nf_instance_t *nf_instance, ogs_sbi_xact_t *xact)
{
    ogs_sbi_send(nf_instance, client_cb, xact);
}

void ausf_sbi_discover_and_send(OpenAPI_nf_type_e target_nf_type,
        ausf_ue_t *ausf_ue, ogs_sbi_session_t *session, void *data,
        ogs_sbi_request_t *(*build)(ausf_ue_t *ausf_ue, void *data))
{
    ogs_sbi_xact_t *xact = NULL;

    ogs_assert(target_nf_type);
    ogs_assert(ausf_ue);
    ogs_assert(session);
    ogs_assert(build);

    xact = ogs_sbi_xact_add(target_nf_type, &ausf_ue->sbi, data,
            (ogs_sbi_build_f)build, ausf_timer_sbi_client_wait_expire);
    ogs_assert(xact);

    xact->assoc_session = session;

    if (ogs_sbi_discover_and_send(xact,
            (ogs_fsm_handler_t)ausf_nf_state_registered, client_cb) != true) {

        ogs_sbi_server_send_error(session,
            OGS_SBI_HTTP_STATUS_GATEWAY_TIMEOUT, NULL,
            "Cannot discover", ausf_ue->suci);
    }
}
