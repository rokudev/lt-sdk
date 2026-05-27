/*******************************************************************************
 * <lt/device/serialport/LTDeviceSerialPort.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup LTDeviceSerialPort LTDeviceSerialPort
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for communicating over serial ports
 *
 * This Library provides access to a platform's serial ports (typically UARTs),
 * represented by, and operated through, LTObjects by the underlying Driver
 */

#ifndef LT_INCLUDE_LT_DEVICE_SERIALPORT_LTDEVICESERIALPORT_H
#define LT_INCLUDE_LT_DEVICE_SERIALPORT_LTDEVICESERIALPORT_H

#include <lt/LTTypes.h>
#include <lt/LTObject.h>
#include <lt/core/LTTime.h>

LT_EXTERN_C_BEGIN

/*______________________________________________________________________________
   Serial Port Object Interface
     In order to access a serial port, a client:
     1. Creates a LTSerialPort LTObject with lt_createobject or
        LT_GetCore()->CreateObject().
     2. Calls Acquire() on the LTObject, providing the name of the serial port
        and the parameters listed below to associate the LTObject with the
        named serial port, and to configure the serial port according to the
        parameters.
        Note: only one client may hold an LTObject representing an individual
        serial port; subsequent attempts to acquire a serial port will fail.
        Parameters:
        a. serial bit rate (bits per second)
        b. synchronous or asynchronous operation
        c. number of data bits
        d. parity (if applicable),
        e. number of stop bits (if applicable),
        NOTE:
             The client MUST call Acquire() on the LTObject before calling any
             other API function.
     3. (optionally) Calls Connect() on the LTObject, providing three pointers:
        a. a pointer to a callback function which accepts characters incoming
           on the serial port (this pointer may be NULL if the client is not
           interested in receive data)
        b. a pointer to a callback function which is called to notify the client
           of changes in the status of the serial port (this pointer may be NULL
           if the client is not interested in status changes)
        c. a pointer to client data which is passed to the callback functions
        (Connect need not be called at all if no notifications are desired,
         and may be called again with different callback function pointers or
         with NULL as one or both of a and b to disconnect.)
     4. Calls any of the Send...() functions on the LTObject to transmit data
        over the serial port
     5. (optionally) Calls Release() on the LTObject to disassociate the LTObject
        from the serial port, and to release the serial port for use by other
        clients, while leaving the LTObject intact.  Further use of a serial
        port through this LTObject requires another call to Acquire().
     6. Destroys the LTObject to discontinue notifications, deinitialize the
        serial-port hardware, and reclaim any memory used in the operation of
        the serial port, and release the serial port for use by other clients.

     For example:

     LTSerialPort *port = lt_createobject(LTSerialPort);
     if (port) {
         if (port->API->Acquire(port, "remote_terminal", 115200,
                                LTDeviceSerialPort_Asynchronous, false
                                8, LTDeviceSerialPort_NoParity, 1)) {
             port->API->Connect(port, ReceiveSerialCharsProc, NULL, NULL);
             port->API->SendChars(port, "Hello\n", 11);
             port->API->Release(port);
         }
         (...)
         if (port->API->Acquire(port, "modem", 9600,
                                LTDeviceSerialPort_Asynchronous, false
                                8, LTDeviceSerialPort_NoParity, 1) {
             port->API->SendChars("atdt18008675309\n", 6);
             (...)
         }
         lt_destroyobject(port);
     }

     LTSerialPort *port = lt_createobject(LTSerialPort);
     if (port) {
         if (port->API->Acquire(port, 230400, "IPC", 230400,
                                LTDeviceSerialPort_Asynchronous, false
                                8, LTDeviceSerialPort_NoParity, 1) {
             port->API->Connect(port, RxCharsProc, StatusProc, pointerToMyImportantStruct);
             port->API->SendChars("Initialize\n", 11);
             (...)
         }
         lt_destroyobject(port);
     }
*/

/*______________________________________________________________________________
   LTSerialPort LTObject Interface                                            */

/* Line status notification values: */
typedef enum LTDeviceSerialPort_PortStatus {
    LTDeviceSerialPort_PortStatus_Normal           = 0,
    LTDeviceSerialPort_PortStatus_BreakReceived    = 1 << 0,
    LTDeviceSerialPort_PortStatus_Overrun          = 1 << 1,
    LTDeviceSerialPort_PortStatus_FramingError     = 1 << 2,
    LTDeviceSerialPort_PortStatus_ParityError      = 1 << 3,
    LTDeviceSerialPort_PortStatus_RxBufferOverrun  = 1 << 4,
} LTDeviceSerialPort_PortStatus;

/* Parity: */
typedef enum LTDeviceSerialPort_Parity {
    LTDeviceSerialPort_NoParity    = 0,
    LTDeviceSerialPort_OddParity,
    LTDeviceSerialPort_EvenParity
} LTDeviceSerialPort_Parity;

typedef void (LTDeviceSerialPort_ReceiveCharsProc)(const char *chars, u32 nChars, void *clientData);
/**<
 * @brief Callback function to accept characters received on the serial port.
 *
 * @param chars pointer to the characters received
 * @param nChars number of characters available at %chars
 * @param clientData client data pointer passed to Connect()
 */

typedef void (LTDeviceSerialPort_ReceiveStatusProc)(LTDeviceSerialPort_PortStatus status, void *clientData);
/**<
 * @brief Callback function to accept status-change notifications from the serial port.
 *
 * @param status the current status of the serial port
 * @param clientData client data pointer passed to Connect()
 */

typedef_LTObject(LTSerialPort, 1) {
    bool (*Acquire)(LTSerialPort *port, const char *portName, u32 nBitsPerSecond, bool bSynchronous,
                    u32 nDataBits, LTDeviceSerialPort_Parity parity, u32 nStopBits);
    /**<
     * @brief Obtain control of and configure the serial port.
     *
     * @param port pointer to the serial port LTObject
     * @param portName the name of the serial port to obtain and configure
     * @param nBitsPerSecond the transmit and receive data rate in bits per second
     * @param bSynchronous false for asynchronous, true for synchronous (only applicable if the
     *        serial port object represents a synchronous serial port (typically a USRT or USART)
     * @param nDataBits the number of data bits per character.  An invalid number of data bits
     *        (determined by the Driver) will result in failure to configure the serial port.
     * @param parity the parity used for error checking (if appropriate - this parameter may
     *        be ignored by the Driver if parity is irrelevant or cannot be configured by the
     *        client)
     * @param stopBits the number of stop bits per character (if appropriate - this parameter
     *        may be ignored by the Driver if stop bits cannot be configured by the client)
     *
     * @return true if the configuration was successful, false otherwise
     */

    void (*Release)(LTSerialPort *port);
    /**<
     * @brief Disassociate the LTObject from the serial port, and release the serial port for
     *        use by other clients.
     *
     * @param port pointer to the serial port LTObject
     */

    void (*Connect)(LTSerialPort *port,
                    LTDeviceSerialPort_ReceiveCharsProc  *receiveCharsProc,
                    LTDeviceSerialPort_ReceiveStatusProc *receiveStatusProc,
                    void *clientData);
    /**< @brief provide callback function pointers to receive characters and status changes
     *
     * @param port pointer to the serial port LTObject
     * @param receiveCharsProc pointer to the callback function which will accept characters
     *        received by the serial port when they become available.  If NULL, received characters
     *        are quietly dropped by the Driver.
     * @param receiveStatusProc pointer to the callback function which will accept status-change
     *        notifications from the serial port.  If NULL, no status-change-notification calls
     *        will be made.
     * @param clientData client data passed to %receiveCharsProc and %receiveStatusProc
     */

    void (*SendChars)(LTSerialPort *port, const char *chars, u32 nChars);
    /**<
     * @brief Transmit a character string of a given length over the serial port.
     *
     * @note This function will return after the last char at %chars has been committed to
     *       the serial port hardware.  This will likely be before that char has actually
     *       shifted out onto the wire, due to FIFO/buffering in the serial port hardware.
     *
     * @param port pointer to the serial port object
     * @param chars pointer to the chars to transmit
     * @param nChars number of chars at %chars to transmit
     *
     * @see %SendCString %SendChar
     */

    void (*SendCString)(LTSerialPort *port, const char *string);
    /**<
     * @brief Transmit a null-terminated string over the serial port.  The null terminator
     *        is not transmitted.
     *
     * @note This function will return after the last char at %chars has been committed to
     *       the serial port hardware.  This will likely be before that char has actually
     *       shifted out onto the wire, due to FIFO/buffering in the serial port hardware.
     *
     * @param port pointer to the serial port object
     * @param chars pointer to the null-terminated string to transmit
     *
     * @see %SendChars %SendChar
     */

    void (*SendChar)(LTSerialPort *port, char c);
    /**<
     * @brief Transmit a null-terminated string over the serial port.  The null terminator
     *        is not transmitted.
     *
     * @note This function will return after the char has been committed to the serial port
     *       hardware.  This will likely be before the char has actually shifted out onto
     *       the wire, due to FIFO/buffering in the serial port hardware.
     *
     * @param port pointer to the serial port object
     * @param c the character to transmit
     *
     * @see %SendChars %SendCString
     */

    void (*SendBreak)(LTSerialPort *port, LTTime breakDuration);
    /**<
     * @brief Send a break on the transmit line.  Typically, this involves driving the
     *        transmit line to its mark state for longer than the duration of a single
     *        frame.
     *
     * @param port pointer to the serial port object
     * @param breakDuration the duration of the break, if the client is allowed to
     *        specify duration.  Passing 0 as %breakDuration results in transmission of
     *        a break of default duration.
     */

} LTOBJECT_API;

/*______________________________________________________________________________
   Device Library Root Interface                                              */

typedef bool (LTDeviceSerialPort_EnumerateSerialPortProc)(const char *serialPortName, void *clientData);
    /**<
     * @brief Callback for serial port enumeration
     *
     * @param serialPortName the name of the serial port
     * @param clientData client data pointer
     *
     * @return true to continue enumeration, false to stop
     */

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTDeviceSerialPort, 1);

struct LTDeviceSerialPortApi {
    INHERIT_LIBRARY_BASE

    u32 (*EnumerateSerialPorts)(LTDeviceSerialPort_EnumerateSerialPortProc *enumerationProc, void *clientData);
    /**<
     * @brief Enumerate the available serial ports
     *
     * For Unit Tests, to create a list of serial ports to test.
     *
     * @param enumerationProc callback to return the serial port name
     * @param clientData client data pointer
     * @return the number of serial ports enumerated
     */
};

/*_________________________________________________________________________________________________________________________
   Driver Library Root Interface

   IMPLEMENTING DRIVER-LEVEL SUPPORT FOR LTDeviceSerialPort
   ========================================================

     LTDeviceSerialPort is designed to make porting to a new platform as easy as possible.  The Driver-level LT Library
     interface and specifications are therefore as simple as possible.  The Driver LT Library (<platform>DriverSerialPort)
     accompanying LTDeviceSerialPort for a given platform must implement two interfaces, both of which are used solely by
     LTDeviceSerialPort and are not accessed directly by any client:

        1. ILTDriverSerialPort - the Driver-level interface

            LTDeviceSerialPort uses this interface to interact with the Driver in contexts wherein individual device
            instances are not relevant.  This interface contains one function, which is used by LTDeviceSerialPort to
            discover all the device instances provided by the Driver.  The function calls the enumerationProc callback
            once for each instance the Driver provides, passing the name of the instance to the callback, and returning
            the number of instances enumerated.  The callback can, through its return value, cause enumeration to stop.

        2. LTDriverSerialPort - the device-instance interface

            Through this interface, LTDeviceSerialPort interacts directly with specific device instances (in this case,
            specific serial ports, typically implemented as UARTs in the hardware).  LTDeviceSerialPort creates (through
            a call to LTCore's CreateObject()) an LTObject defined by the specialization of LTDriverSerialPort provided
            by the Driver, then calls the functions in the LTDriverSerialPort interface to configure and operate the
            underlying serial port.  The client makes similar calls to the interface of a complementary LTObject provided
            by LTDeviceSerialPort to conduct these operations in a platform-independent way, and therefore never inteacts
            directly with the Driver.

            The interface provides four functions which, respectively: claim a device instance and configure it for use
            based on the needs of LTDeviceSerialPort's client; release the device instance for other use; establish the
            data path (by way of callback functions) along which the Driver notifies the client of incoming characters
            received by the serial port or changes to the status of the serial port (such as overrun or parity errors);
            and transmit characters over the serial port.  Acquire() and Release() are required for every platform.
            Connect() is only required for device instances that will receive data or report status back to the client
            (through LTDeviceSerialPort).  PutChars() is required for device instances that will transmit data.  If either
            of these functions are not required, they may be empty, but must exist. */

#ifndef DOXY_SKIP // [
typedef void (LTDriverSerialPort_ReceiveCharsProc)(const char *chars, u32 nChars, void *clientData) LT_ISR_SAFE;
typedef void (LTDriverSerialPort_ReceiveStatusProc)(LTDeviceSerialPort_PortStatus status, void *clientData) LT_ISR_SAFE;

typedef_LTObject(LTDriverSerialPort, 1) {
    bool (*Acquire)(LTDriverSerialPort *port, const char *portName, u32 nBitRate, u16 nStopBits, LTDeviceSerialPort_Parity parity);
        /* Reserve a serial port for use by the client.  Enable the serial port hardware and
           configure it per the inputs.
           Return true if the underlying hardware is successfully acquired and configured, false if the hardware
           was not successfully acquired and configured, or if the device instance is already in use. */
    void (*Release)(LTDriverSerialPort *port);
        /* Release the serial port.  May or may not actually disable the serial port hardware, but should mark
           device instance resources as available again.  LTDeviceSerialPort must call Acquire() again to associate
           the LTObject with an actual device instance. */
    void (*Connect)(LTDriverSerialPort *port,
                    LTDriverSerialPort_ReceiveCharsProc *receiveCharsProc,
                    LTDriverSerialPort_ReceiveStatusProc *receiveStatusProc,
                    void *clientData);
        /* Enable UART interrupts and attach ISR-safe callbacks to accept receive characters and status changes.
           Callbacks are optional, e.g., if only receive characters are relevant, receiveStatusProc can be left NULL.
           If both callbacks are NULL, the Driver may leave UART interrupts disabled. */
    void (*PutChars)(LTDriverSerialPort *port, const char *chars, u32 nChars);
        /* put nChars characters at characters into the serial port's transmit buffer or FIFO for immediate transmission.
           There is no mechanism for transmit-complete notification; this function should block until all chars have
           been committed to the transmit buffer.  Return does not necessarily indicate that all chars have actually
           been shifted out onto the wire. */
} LTOBJECT_API;

/* LTDriverSerialPort Interface - this interface is to be used only by LTDeviceSerialPort. */
typedef_LTLIBRARY_INTERFACE(ILTDriverSerialPort, 1) {
    u32 (*EnumerateSerialPorts)(LTDeviceSerialPort_EnumerateSerialPortProc *enumerationProc, void *clientData);
    /**<
     * @brief Enumerate the available serial ports
     *
     * For Unit Tests, to create a list of serial ports to test.
     *
     * @param enumerationProc callback to return the serial port name
     * @param clientData client data pointer
     * @return the number of serial ports enumerated
     */
} LTLIBRARY_INTERFACE;
#endif  // DOXY_SKIP ]

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_SERIALPORT_LTDEVICESERIALPORT_H */

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Aug-24   constantine created
 */
