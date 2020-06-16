

#ifndef _MGOS_BT_SERIAL
#define _MGOS_BT_SERIAL

class BluetoothSerial;

extern "C"
{

    BluetoothSerial* mgos_bt_serial_create(void);

    void mgos_bt_serial_close(BluetoothSerial* bt);

    bool mgos_bt_serial_begin(BluetoothSerial* bt, const char* localName, bool isMaster);

    char mgos_bt_serial_read_byte(BluetoothSerial* bt);

    int mgos_bt_serial_read(BluetoothSerial* bt, char* buffer, int size);

    int mgos_bt_serial_write(BluetoothSerial* bt, const char* buffer, int size);

    int mgos_bt_serial_peek(BluetoothSerial* bt);

    int mgos_bt_serial_available(BluetoothSerial* bt);

    void mgos_bt_serial_flush(BluetoothSerial* bt);

    void mgos_bt_serial_end(BluetoothSerial* bt);

    bool mgos_bt_serial_has_client(BluetoothSerial* bt);

}


#endif