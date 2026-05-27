/*******************************************************************************
 * <lt/net/httpclient/LTNetHttpClient.h>                  HTTP Client
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_HTTPCLIENT_LTNETHTTPCLIENT_H
#define ROKU_LT_INCLUDE_LT_NET_HTTPCLIENT_LTNETHTTPCLIENT_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef LTHandle LTHttpRequest;

typedef enum {
    kLTHttpRequestMethod_GET,
    kLTHttpRequestMethod_HEAD,
    kLTHttpRequestMethod_POST,
    kLTHttpRequestMethod_PUT,
} LTHttpRequestMethod;

typedef enum {
    LTHttpRequestEvent_HeadComplete,
    LTHttpRequestEvent_BodyDataAvailable,
    LTHttpRequestEvent_BodyPartPending,     // request handler is waiting for next part of a multi-part form
    LTHttpRequestEvent_BodyWriteReady,
    LTHttpRequestEvent_RequestComplete,
    LTHttpRequestEvent_DisconnectPending,   // only occur when server disconnects right after sending the last data, but client didn't complete data processing.
    LTHttpRequestEvent_Disconnected,        // only occur when client already completed the http request, then server disconnects.
    LTHttpRequestEvent_Error,               // socket error or disconnection when http is connecting.
} LTHttpRequestEvent;

typedef enum {
    LTHttpStreamError = -1,
    LTHttpStreamPartDone = -2,
    LTHttpStreamEpilogDone = -3,
} LTHttpPartState;

// If partBody is allocated in heap, then MultipartData must stay in memory until http request is done and destroyed.
typedef struct {
    LTString    partHeaders;    // optional additional part-specific headers sent by HTTP library for this part only
    LTString    partName;       // local storage for part name string, not used by HTTP library
    const char *name;
    const char *filename;
    const char *contentType;
    LTString    partBody;       // local storage for body string data, not used by HTTP library, but must only be freed when http request is done and destroyed.
    const void *data;           // if partBody is not NULL, this must point to its const data and will not be separately freed when the object is destroyed
    u32         size;
} LTHttpMultipartData;
/* The 'filename' field is required for file uploads. */

typedef void (LTHttpClient_RequestEventProc)(LTHttpRequest hRequest, LTHttpRequestEvent event, void * pClientData);
/**< callback for indicating the occurrence of various request events.
 *
 *   This callback is called to indicate to the client that important events in the
 *   request or response process have occurred.
 *
 *   @param hRequest the request instance
 *   @param event the event that has occurred
 *   @param pClientData the client data that was specified when the request was created.
 */

/*  _________________________
    Library Root Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetHttpClient, 1) {
/** ___________________________________
 *   @section httpclient_library LTSystemHttpClient Library
 *   * @brief a library for sending HTTP requests and parsing their responses.
 *   *
 *   * LTSystemHttpClient provides a simple interface for sending HTTP requests to a server and
 *   * parsing their responses
 \______________________________________________________________________________________*/

    LTHttpRequest   (* CreateRequest)(const char * pRequestUrl, LTHttpRequestMethod nMethod, LTHandle hTransport);
        /**< creates a new HTTP request
         *
         *   @param pRequestUrl the URL to which the request will be sent.
         *   @param nMethod the HTTP method for the request.
         *   @return the LTHttpRequest instance
         */
     LTHttpRequest   (*Get)(const char * pRequestUrl, LTHandle hTransport, LTHttpClient_RequestEventProc * pRequestEventProc, void * pClientData);
        /**< creates and sends an HTTP GET request
         *
         *   Convenience method for creating and sending a simple GET request. If parameters or headers
         *   need to be added, use %SendGetRequestWithParams or %CreateRequest() to manually create and
         *   configure the request.
         *
         *   @param pRequestUrl the URL to which the request will be sent.
         *   @param pRequestEventProc the request event callback function
         *   @param pClientData client data pointer, to be passed back to callback function
         *   @return the LTHttpRequest instance
         */
    LTHttpRequest   (*GetWithParams)(const char * pRequestUrl, LTHandle hTransport, LTHttpClient_RequestEventProc * pRequestEventProc, void * pClientData, u32 nNumParams, ...);
        /**< creates and sends an HTTP GET request
         *
         *   Convenience method for creating and sending a simple GET request. If headers need
         *   to be added, use %CreateRequest() to manually create and configure the request.
         *
         *   @param pRequestUrl the URL to which the request will be sent.
         *   @param pRequestEventProc the request event callback function
         *   @param pClientData client data pointer, to be passed back to callback function
         *   @param nNumParams the number of param string pairs which follow as varargs
         *   @param ... The request parameters to be added. Each request param must include two function args:
         *              const char * pParamName - the name of the parameter
         *              const char * pParamValue - the parameter value.
         *   @return the LTHttpRequest instance
         */
    LTHttpRequest   (*Post)(const char * pRequestUrl, LTHandle hTransport, LTHttpClient_RequestEventProc * pRequestEventProc, void * pClientData, u32 nNumParams, ...);
    LTHttpRequest    (*Put)(const char * pRequestUrl, LTHandle hTransport, LTHttpClient_RequestEventProc * pRequestEventProc, void * pClientData, u32 nNumParams, ...);
         /**< creates and sends an HTTP POST or PUT request
          *
          *   Convenience method for creating and sending a simple POST or PUT request. If headers need
          *   to be added, use %CreateRequest() to manually create and configure the request.
          *
          *   @param pRequestUrl the URL to which the request will be sent.
          *   @param pRequestEventProc the request event callback function
          *   @param pClientData client data pointer, to be passed back to callback function
          *   @param nNumParams the number of param string pairs which follow as varargs
          *   @param ... The request parameters to be added. Each request param must include two function args:
          *              const char * pParamName - the name of the parameter
          *              const char * pParamValue - the parameter value.
          *   @return the LTHttpRequest instance
          */

} LTLIBRARY_INTERFACE;

/*  _________________________
    HTTP Request Interface */
typedef_LTLIBRARY_INTERFACE(ILTHttpRequest, 1) {
/** ___________________________________
 *   @section httpclient_response_event HTTP response event related methods
 *   * @brief HTTP response event methods.
 *   *
 *   * The following methods are used to register/unregister HTTP response event handlers
 \______________________________________________________________________________________*/
    void         (* OnRequestEvent)(LTHttpRequest                   hRequest,
                                    LTHttpClient_RequestEventProc * pRequestEventProc,
                                    void                          * pClientData);
        /**< register for a callback for request events.
         *
         *   @param hRequest the HTTP request instance
         *   @param pRequestEventProc the request event callback function
         *   @param pClientData client data pointer, to be passed back to callback function
         */
    void         (* NoRequestEvent)(LTHttpRequest                   hRequest,
                                    LTHttpClient_RequestEventProc * pRequestEventProc);
       /**< unregister a previously registered request event callback
        *
        *   @param hRequest the HTTP request instance
        *   @param pRequestEventProc the request event callback function
        */

    void         (* EnableDebug)(LTHttpRequest hRequest);
        /**< enable low-level request debugging for this request
         *
         *   @param hRequest   the request handle
         */

/** ___________________________________
 *   @section httpclient_request HTTP request related methods
 *   * @brief HTTP request methods.
 *   *
 *   * The following methods are used to build an HTTP request.
 \______________________________________________________________________________________*/

    void         (* SetEndpoint)(LTHttpRequest hRequest, const char * pEndpoint);
        /**< sets an endpoint to the request
         *
         *   @param hRequest   the request handle
         *   @param pEndpoint  the endpoint of the request
         */
    void         (* AddHeader)(LTHttpRequest hRequest, const char * pName, LTArgType argType, ... /* pValue */);
        /**< adds a header field to the request
         *
         *   @param hRequest the request to which the header should be added
         *   @param pName the name of the header field
         *   @param argType the type of the header value to be added
         *   @param pValue the value of the header field
         */
    void         (* ResetHeader)(LTHttpRequest hRequest);
        /**< destroy header string of the request and create a new one */
    void         (* AddParam)(LTHttpRequest hRequest, const char * pName, LTArgType argType, ... /* pValue */);
        /**< adds a query-string parameter to the request
         *
         *   @param hRequest the request to which the parameter should be added
         *   @param pName the name of the parameter
         *   @param argType the type of the parameter value to be added
         *   @param pValue the value of the parameter
         */
    void         (* SetBody)(LTHttpRequest hRequest, const char * pContentType, const void * pBody, u32 nBodyLength);
        /**< sets body data to be sent in the request
         *
         *   Sets the raw or multipart body data to be sent in the request.
         *   A Content-Type header will be added to the request with the value
         *   specified by pContentType. If a pContentType value of "multipart/form-data"
         *   is specified, the pBody parameter will be interpreted as an array of
         *   LTHttpMultipartData's containing information about the parts to be sent
         *   and nBodyLength will be interpreted as the number elements in that array.
         *
         *   If not all multipart data is available at the time of the call, use AddPendingParts
         *   to add additional parts to the request (not exceeding the total number of parts
         *   defined when SetBody() is called). Incomplete multipart forms will be uploaded
         *   using HTTP1.1 chunked Transfer-Encoding.
         *
         *   @param hRequest the request to which the parameter should be added
         *   @param pContentType the content type corresponding to the body data
         *   @param pBody the body data to be sent
         *   @param nBodyLength the length of the body data
         */
    void         (* AddPendingParts)(LTHttpRequest hRequest, const LTHttpMultipartData * pParts, u32 numParts);
        /**< adds pending parts to multipart data in a request
         *
         *   If the request has a body with a content type of "multipart/form-data",
         *   and there are missing parts (defined as any part with a NULL content
         *   type), this function can add one or more of the missing parts and, if
         *   required, restart the HTTP transmission. It is not necessary to add
         *   all parts at once, but the total number of parts available must not
         *   exceed the number of parts defined when SetBody() was called.
         *
         *   If pParts is NULL and numParts is 0 it indicates that no more parts will
         *   be added. The HTTP upload will conclude when the currently-available
         *   parts have been sent.
         *
         *   @param hRequest the request to which the parameter should be added
         *   @param pParts array of additional parts to add to the request
         *   @param numParts the number of additional parts to add to the request
         */
    void         (* SetTlsSpec)(LTHttpRequest hRequest, const char * pTlsSpec);
        /**< sets TLS options spec string to be used for HTTPS requests
         *
         *   Sets TLS options spec string to be used for HTTPS requests. The TLS
         *   spec string contains a sequence of key:value pairs. LTNetHttpClient
         *   will automatically add the appropriate 'alpn' option. Other options
         *   must be specified by the user. Consult LTNetTls for a list of supported
         *   options.
         *
         *   @param hRequest the request to which the parameter should be added
         *   @param pTlsSpec the TLS spec string
         */
    bool         (* SendRequest)(LTHttpRequest hRequest);
        /**< sends an HTTP request
         *
         *   @param hRequest the request to be sent
         *   @return true if the request was sent successfully, else false
         */

/** ___________________________________
 *   @section httpclient_response HTTP response related methods
 *   * @brief HTTP response methods.
 *   *
 *   * The following methods are used to query information about the HTTP response that was
 *   * returned from the server. They should only be called after the response HEAD data has
 *   * been received and parsed. (i.e. inside of the LTHttpClient_ResponseHeadCompleteEventProc callback
 *   * or after it completes)
 \______________________________________________________________________________________*/
    u32          (* GetResponseStatusCode)(LTHttpRequest hRequest);
        /**< gets the HTTP status code returned from the server
         *
         *   @param hRequest the HTTP request instance
         *   @return the HTTP status code received from the server
         */
    u32          (* GetResponseContentLength)(LTHttpRequest hRequest);
        /**< gets the total length of the message body content to be received over successive calls
          *   to the specified LTHttpClient_ResponseBodyDataEventProc.
          *
          *   @param hRequest the HTTP request instance
          *   @return the value of the Content-Length header received from the server or 0 if no
          *           Content-Length was specified.
          */
    const char * (* GetResponseHeader)(LTHttpRequest hRequest, const char * pHeaderName);
        /**< gets the value of the specified response header field.
          *
          *   @param hRequest the HTTP request instance
          *   @return the value of the specified response header field name, or NULL is that header was
          *           not included in the response.
          */
    u32          (* BodyDataAvailable)(LTHttpRequest hRequest);
        /**< gets the number of data bytes available to be read.
          *
          *   @param hRequest the HTTP request instance
          *   @return gets the number of data bytes available to be read.
          */
    s32          (* ReadBodyData)(LTHttpRequest hRequest, void * pData, LT_SIZE nDataLen);
        /**< reads body data
          *
          *   @param hRequest the HTTP request instance
          *   @param pData the buffer into which body data will be copied
          *   @param nDataLen the maximum number of bytes that can be copied
          *   @return the actual number of bytes that were copied into pData, or negative on errors.
          */
    s32          (* WriteBodyData)(LTHttpRequest hRequest, const void * pData, LT_SIZE nDataLen);
         /**< writes body data
           *
           *   @param hRequest the HTTP request instance
           *   @param pData the buffer containing body data to be written
           *   @param nDataLen number of bytes to be written
           *   @return the actual number of bytes written, or negative on errors. When 0 bytes are written, the write should
           *           be retried instead of waiting for a new BodyWriteReady event. 0 can represent internal re-sending by the (TLS)
           *           socket, which will not trigger a new BodyWriteReady event.
           */

    s32          (* StreamBodyData)(LTHttpRequest hRequest, void *pData, LT_SIZE nDataLen);
        /**< streams body data to socket
          *
          *   @param hRequest the HTTP request instance
          *   @param pData the buffer containing body data to be written
          *   @param nDataLen number of bytes to be written
          *   @return the actual number of bytes written, or negative on errors.
          */

    s32          (* WritePartBoundary)(LTHttpRequest hRequest);
        /**< writes the part boundary in the multipart request, allowing the program to begin writing
          *  the next part. The part boundary can either be a part header or part epilog
          *
          *   @param hRequest the HTTP request instance
          *   @return enum LTHttpPartState that indicates the status of the write
          */

    bool         (* ResetRequest)(LTHttpRequest hRequest);
        /**< Reset the internal data of a request, including event handle, response, outgoing string, and socket.
         *
         *   @param hRequest  The request
         *   @return true if the request is reset and recreated.
         *           false on failure.
         */

    void         (* DestroyRequestSocket)(LTHttpRequest hRequest);
        /**< Destroy request's socket. */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_HTTPCLIENT_LTNETHTTPCLIENT_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-Feb-22   trajan      created
 *  29-Sep-23   aurelian    added http queue system
 *  22-Jul-24   decius      added streaming function
 */