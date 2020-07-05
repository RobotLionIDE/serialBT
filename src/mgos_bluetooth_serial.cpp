#include <mgos_bluetooth_serial.h>
#include <bluetooth_serial.h>

extern "C" 
{

    BluetoothSerial* mgos_bt_serial_create(void)
    {
        return new BluetoothSerial();
    }

    void mgos_bt_serial_close(BluetoothSerial* bt)
    {
        if (!bt) return;
        delete bt;
    }

    bool mgos_bt_serial_begin(BluetoothSerial* bt, const char* localName, bool isMaster)
    {
        if (!bt) return false;
        return bt->begin(localName, isMaster);
    }

    char mgos_bt_serial_read_byte(BluetoothSerial* bt)
    {
        if (!bt) return -1;
        return bt->read();
    }

    int mgos_bt_serial_read(BluetoothSerial* bt, char* buffer, int size)
    {
        if (!bt) return 0;
        return bt->read(buffer, size);
    }

    int mgos_bt_serial_write(BluetoothSerial* bt, const char* buffer, int size)
    {
        if (!bt) return 0;
        return bt->write(buffer, size);
    }

    int mgos_bt_serial_peek(BluetoothSerial* bt)
    {
        if (!bt) return -1;
        return bt->peek();
    }

    int mgos_bt_serial_available(BluetoothSerial* bt)
    {
        if (!bt) return 0;
        return bt->available();
    }

    void mgos_bt_serial_flush(BluetoothSerial* bt)
    {
        if (!bt) return;
        bt->flush();
    }

    void mgos_bt_serial_end(BluetoothSerial* bt)
    {
        if (!bt) return;
        return bt->end();
    }

    bool mgos_bt_serial_has_client(BluetoothSerial* bt)
    {
        if (!bt) return false;
        return bt->hasClient();
    }

}
