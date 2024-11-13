# UART-Shell
ex. for pthread , termios , signals , input mode "canonical or raw"

to use this shell.

1. check on serial port first
   ```bash
   sudo dmesg | grep tty
   ```

2. compile uart_shell.c
   ```bash
   gcc -o uart_shell uart_shell.c
   ```

3. run shell
   ```bash
   sudo ./uart_shell /dev/ttyUSB<x> <boudrate>
   ```
(==ttyUSBx==) is your serial port
supported (==boudrate==) are "9600" , "19200" , "38400" , "57600" , "115200"

4.  now you can transmit and receive normally.
5. if you want to receive on file
   ```bash
   R>file
   ```
6. back to receive on shell
   ```bash
   R>shell
   ```
7. if you want to transmit file
   ```bash
   T<file
```

this shell supported "==Empty Enter==" , "==back Space==" , "==Receive while incompletely transmit=="

