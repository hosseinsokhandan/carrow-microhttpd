/*
     This file is part of libmicrohttpd
     Copyright (C) 2010, 2011, 2012 Daniel Pittman and Christian Grothoff
     Copyright (C) 2014-2022 Evgeny Grin (Karlson2k)

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
/**
 * @file basicauth.c
 * @brief Implements HTTP basic authentication methods
 * @author Amr Ali
 * @author Matthieu Speder
 * @author Karlson2k (Evgeny Grin)
 */
#include "basicauth.h"
#include "gen_auth.h"
#include "platform.h"
#include "mhd_limits.h"
#include "internal.h"
#include "base64.h"
#include "mhd_compat.h"
#include "mhd_str.h"


/**
 * Get request's Basic Authorisation parameters.
 * @param connection the connection to process
 * @return pointer to request Basic Authorisation parameters structure if
 *         request has such header (allocated in connection's pool),
 *         NULL otherwise.
 */
static const struct MHD_RqBAuth *
get_rq_bauth_params (struct MHD_Connection *connection)
{
  const struct MHD_AuthRqHeader *rq_params;

  rq_params = MHD_get_auth_rq_params_ (connection);
  if ( (NULL == rq_params) ||
       (MHD_AUTHTYPE_BASIC != rq_params->auth_type) )
    return NULL;

  return rq_params->params.bauth;
}


/**
 * Get the username and password from the Basic Authorisation header
 * sent by the client
 *
 * @param connection the MHD connection structure
 * @return NULL not valid Basic Authentication header is present in
 *         current request, or pointer to structure with username and
 *         password, which must be freed by #MHD_free().
 * @note Available since #MHD_VERSION 0x00097517
 * @ingroup authentication
 */
_MHD_EXTERN struct MHD_BasicAuthInfo *
MHD_basic_auth_get_username_password3 (struct MHD_Connection *connection)
{
  const struct MHD_RqBAuth *params;
  char *decoded;
  size_t decoded_len;
  struct MHD_BasicAuthInfo *ret;
  size_t username_len;
  char *colon;
  size_t password_pos;
  size_t password_len;

  params = get_rq_bauth_params (connection);

  if (NULL == params)
    return NULL;

  if ((NULL == params->token68.str) || (0 == params->token68.len))
    return NULL;

  decoded = BASE64Decode (params->token68.str, params->token68.len,
                          &decoded_len);
  if ((NULL == decoded) || (0 == decoded_len))
  {
#ifdef HAVE_MESSAGES
    MHD_DLOG (connection->daemon,
              _ ("Error decoding Basic Authorization authentication.\n"));
#endif /* HAVE_MESSAGES */
    if (NULL != decoded)
      free (decoded);
    return NULL;
  }
  colon = memchr (decoded, ':', decoded_len);
  if (NULL != colon)
  {
    username_len = (size_t) (colon - decoded);
    password_pos = username_len + 1;
    password_len = decoded_len - password_pos;
  }
  else
    username_len = decoded_len;

  ret = (struct MHD_BasicAuthInfo *) malloc (sizeof(struct MHD_BasicAuthInfo)
                                             + decoded_len + 1);
  if (NULL == ret)
  {
    free (decoded);
#ifdef HAVE_MESSAGES
    MHD_DLOG (connection->daemon,
              _ ("Failed to allocate memory to process " \
                 "Basic Authorization authentication.\n"));
#endif /* HAVE_MESSAGES */
    return NULL;
  }

  ret->username = (char *) (ret + 1);
  memcpy (ret->username, decoded, decoded_len);
  free (decoded);
  ret->username[username_len] = 0; /* Zero-terminate the string */
  ret->username_len = username_len;
  if (NULL != colon)
  {
    mhd_assert (decoded_len >= password_pos);
    mhd_assert (decoded_len > password_pos || 0 == password_len);
    mhd_assert (decoded_len > password_len);
    mhd_assert (decoded_len > username_len);
    ret->password = ret->username + password_pos;
    ret->password[password_len] = 0; /* Zero-terminate the string */
    ret->password_len = password_len;
  }
  else
  {
    ret->password = NULL;
    ret->password_len = 0;
  }

  return ret;
}


/**
 * Get the username and password from the basic authorization header sent by the client
 *
 * @param connection The MHD connection structure
 * @param[out] password a pointer for the password, free using #MHD_free().
 * @return NULL if no username could be found, a pointer
 *      to the username if found, free using #MHD_free().
 * @deprecated use #MHD_basic_auth_get_username_password3()
 * @ingroup authentication
 */
_MHD_EXTERN char *
MHD_basic_auth_get_username_password (struct MHD_Connection *connection,
                                      char **password)
{
  const struct MHD_RqBAuth *params;
  char *decode;
  size_t decode_len;
  const char *separator;

  params = get_rq_bauth_params (connection);

  if (NULL == params)
    return NULL;

  if ((NULL == params->token68.str) || (0 == params->token68.len))
    return NULL;

  decode = BASE64Decode (params->token68.str, params->token68.len, &decode_len);
  if ((NULL == decode) || (0 == decode_len))
  {
#ifdef HAVE_MESSAGES
    MHD_DLOG (connection->daemon,
              _ ("Error decoding basic authentication.\n"));
#endif
    if (NULL != decode)
      free (decode);
    return NULL;
  }
  /* Find user:password pattern */
  if (NULL != (separator = memchr (decode,
                                   ':',
                                   decode_len)))
  {
    char *user;
    size_t user_len;
    size_t password_len;

    user = decode; /* Reuse already allocated buffer */
    user_len = (size_t) (separator - decode);
    user[user_len] = 0;

    if (NULL == password)
      return user;

    password_len = decode_len - user_len - 1;
    *password = (char *) malloc (password_len + 1);
    if (NULL != *password)
    {
      if (0 != password_len)
        memcpy (*password, decode + user_len + 1, password_len);
      (*password)[password_len] = 0;

      return user;
    }
#ifdef HAVE_MESSAGES
    else
      MHD_DLOG (connection->daemon,
                _ ("Failed to allocate memory.\n"));
#endif /* HAVE_MESSAGES */
  }
#ifdef HAVE_MESSAGES
  else
    MHD_DLOG (connection->daemon,
              _ ("Basic authentication doesn't contain ':' separator.\n"));
#endif
  free (decode);
  return NULL;
}


/**
 * Queues a response to request basic authentication from the client.
 *
 * The given response object is expected to include the payload for
 * the response; the "WWW-Authenticate" header will be added and the
 * response queued with the 'UNAUTHORIZED' status code.
 *
 * See RFC 7617#section-2 for details.
 *
 * The @a response is modified by this function. The modified response object
 * can be used to respond subsequent requests by #MHD_queue_response()
 * function with status code #MHD_HTTP_UNAUTHORIZED and must not be used again
 * with MHD_queue_basic_auth_fail_response3() function. The response could
 * be destroyed right after call of this function.
 *
 * @param connection the MHD connection structure
 * @param realm the realm presented to the client
 * @param prefer_utf8 if not set to #MHD_NO, parameter'charset="UTF-8"' will
 *                    be added, indicating for client that UTF-8 encoding
 *                    is preferred
 * @param response the response object to modify and queue; the NULL
 *                 is tolerated
 * @return #MHD_YES on success, #MHD_NO otherwise
 * @note Available since #MHD_VERSION 0x00097516
 * @ingroup authentication
 */
_MHD_EXTERN enum MHD_Result
MHD_queue_basic_auth_fail_response3 (struct MHD_Connection *connection,
                                     const char *realm,
                                     int prefer_utf8,
                                     struct MHD_Response *response)
{
  static const char prefix[] = "Basic realm=\"";
  static const char suff_charset[] = "\", charset=\"UTF-8\"";
  static const size_t prefix_len = MHD_STATICSTR_LEN_ (prefix);
  static const size_t suff_simple_len = MHD_STATICSTR_LEN_ ("\"");
  static const size_t suff_charset_len =
    MHD_STATICSTR_LEN_ (suff_charset);
  enum MHD_Result ret;
  char *h_str;
  size_t h_maxlen;
  size_t suffix_len;
  size_t realm_len;
  size_t realm_quoted_len;
  size_t pos;

  if (NULL == response)
    return MHD_NO;

  suffix_len = (0 == prefer_utf8) ? suff_simple_len : suff_charset_len;
  realm_len = strlen (realm);
  h_maxlen = prefix_len + realm_len * 2 + suffix_len;

  h_str = (char *) malloc (h_maxlen + 1);
  if (NULL == h_str)
  {
#ifdef HAVE_MESSAGES
    MHD_DLOG (connection->daemon,
              "Failed to allocate memory for Basic Authentication header.\n");
#endif /* HAVE_MESSAGES */
    return MHD_NO;
  }
  memcpy (h_str, prefix, prefix_len);
  pos = prefix_len;
  realm_quoted_len = MHD_str_quote (realm, realm_len, h_str + pos,
                                    h_maxlen - prefix_len - suffix_len);
  pos += realm_quoted_len;
  mhd_assert (pos + suffix_len <= h_maxlen);
  if (0 == prefer_utf8)
  {
    h_str[pos++] = '\"';
    h_str[pos++] = 0; /* Zero terminate the result */
    mhd_assert (pos <= h_maxlen + 1);
  }
  else
  {
    /* Copy with the final zero-termination */
    mhd_assert (pos + suff_charset_len <= h_maxlen);
    memcpy (h_str + pos, suff_charset, suff_charset_len + 1);
    mhd_assert (0 == h_str[pos + suff_charset_len]);
  }

  ret = MHD_add_response_header (response,
                                 MHD_HTTP_HEADER_WWW_AUTHENTICATE,
                                 h_str);
  free (h_str);
  if (MHD_NO != ret)
  {
    ret = MHD_queue_response (connection,
                              MHD_HTTP_UNAUTHORIZED,
                              response);
  }
  else
  {
#ifdef HAVE_MESSAGES
    MHD_DLOG (connection->daemon,
              _ ("Failed to add Basic Authentication header.\n"));
#endif /* HAVE_MESSAGES */
  }
  return ret;
}


/**
 * Queues a response to request basic authentication from the client
 * The given response object is expected to include the payload for
 * the response; the "WWW-Authenticate" header will be added and the
 * response queued with the 'UNAUTHORIZED' status code.
 *
 * @param connection The MHD connection structure
 * @param realm the realm presented to the client
 * @param response response object to modify and queue; the NULL is tolerated
 * @return #MHD_YES on success, #MHD_NO otherwise
 * @deprecated use MHD_queue_basic_auth_fail_response3()
 * @ingroup authentication
 */
_MHD_EXTERN enum MHD_Result
MHD_queue_basic_auth_fail_response (struct MHD_Connection *connection,
                                    const char *realm,
                                    struct MHD_Response *response)
{
  return MHD_queue_basic_auth_fail_response3 (connection, realm, MHD_NO,
                                              response);
}


/* end of basicauth.c */
