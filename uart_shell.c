/*
 * author   : Yousef Khalefa          
 * data     : 8-Nov-2024              
 * object   : uart-shell              
 * Linkedin : https://linkedin.com/in/yousef-khalefa 
 * Github   : https://github.com/yousefkhalefa72     
 **/

/************************************** Includes *************************************************/
#include <stdio.h>      // For (printf, getchar)
#include <stdlib.h>     // For (exit, malloc)
#include <unistd.h>     // For (read, write, sleep)
#include <fcntl.h>      // For (open with O_RDWR flag)
#include <termios.h>    // For terminal control (e.g., configuring UART settings)
#include <string.h>     // For (strcmp)
#include <errno.h>      // For (perror)
#include <pthread.h>    // For (pthread_create, pthread_mutex)
#include <signal.h>     // For (SIGINT)
#include <sys/stat.h>   // For (S_IRUSR, S_IWUSR)

/*************************************** Define Types ********************************************/
typedef signed char StdReturn;
#define E_NOK           -1
#define E_OK            0

#define CANONICAL_MODE  0
#define RAW_MODE        1

#define OUT_FLAG_SHELL  0
#define OUT_FLAG_DEST   1  

/*************************************** Defines *************************************************/
#define BUF_SIZE 256    // buffer size for reading input from UART

/************************************** Global Vars **********************************************/
char user_input[BUF_SIZE];                  // Buffer to store user input
unsigned int user_input_counter = 0;        // Counter for the number of characters entered by the user
int uart_fd, dest_fd = -1,source_fd = -1;   // File descriptor for UART communication, received file
char OUT_FLAG = OUT_FLAG_SHELL;             // received direction flag

pthread_mutex_t uart_lock;              // Mutex lock to protect UART access across threads
pthread_t read_tid, write_tid;          // Threads for reading and writing UART data

/*************************************** Functions declaration ************************************/
// Function to delete characters from the terminal (used for backspace functionality)
StdReturn delete_chars(unsigned int number);
// Function to set terminal input mode (canonical or raw) mode
StdReturn set_input_mode(char mode);
// Function to map baudrate string to baudrate constant
speed_t get_baudrate(const char *baudrate_str);
// Function to configure UART settings (baud rate, data bits, stop bits, and parity)
int setup_uart(const char *device, speed_t baudrate); 
// Function to write data to the UART device
int write_uart(const char *data);
// Function to continuously read data from the UART and display it to the user
void* read_uart(void* arg);
// Function to continuously prompt the user for input and send it over UART
void* write_thread(void* arg);
// Function to clean up resources and exit the program gracefully
void cleanup_and_exit();
// Signal handler for SIGINT (Ctrl+C)kill-signal to cleanly exit
void sigint_handler(int sig);

/****************************************** Main program ********************************************/
int main(int argc, char *argv[]) 
{
    if (argc != 3) // handle user fault 
    {
        fprintf(stderr, "Usage: %s <tty_device> <baud_rate>\n", argv[0]);
        return E_NOK;  // Exit if incorrect arguments are provided
    }
    else
    {
        speed_t boudrate = get_baudrate(argv[2]); // Convert string baudrate to constant value

        if (setup_uart(argv[1], boudrate) < 0) // UART setup
        {
            return E_NOK;  // Exit if UART setup fails
        }
        else 
        {
            printf("success to open %s serial port with boudrate %s.\n",argv[1],argv[2]);
        }
    }
    
    signal(SIGINT, sigint_handler);  // Register SIGINT signal handler

    pthread_mutex_init(&uart_lock, NULL);  // Initialize the mutex lock

    // Create the read and write threads
    if (pthread_create(&read_tid, NULL, read_uart, NULL) != 0) 
    {
        perror("Error creating read thread");
        return E_NOK;
    }

    if (pthread_create(&write_tid, NULL, write_thread, NULL) != 0) 
    {
        perror("Error creating write thread");
        return E_NOK;
    }

    set_input_mode(RAW_MODE);  // Set the terminal to raw input mode

    // Wait for threads to finish
    pthread_join(read_tid, NULL);
    pthread_join(write_tid, NULL);

    set_input_mode(CANONICAL_MODE);  // Reset terminal input mode

    if(dest_fd>=0)
        close(dest_fd); // Close dest file descriptor

    close(uart_fd);  // Close UART file descriptor
    pthread_mutex_destroy(&uart_lock);  // Destroy the mutex lock

    return E_OK;  // Exit successfully
}

/************************************* functions *****************************************/
// Function to delete characters from the terminal (used for backspace functionality)
StdReturn delete_chars(unsigned int number)
{
    if(number)
    {
        for (int i = 0; i < number; i++)
        {
            printf("\b \b");  // Move the cursor back and overwrite with space
        }
        return E_OK;
    }
    else
    {
        return E_NOK;
    }
}

// Function to set terminal input mode (canonical or raw) mode
StdReturn set_input_mode(char mode)
{
    struct termios termios_struct;
    tcgetattr(STDIN_FILENO, &termios_struct);  // Get the current terminal settings

    if(mode == CANONICAL_MODE)
    {
        termios_struct.c_lflag |= (ICANON | ECHO);  // Enable canonical mode and echoing
    }
    else if (mode == RAW_MODE)
    {
        termios_struct.c_lflag &= ~(ICANON | ECHO);  // Disable canonical mode and echoing
        termios_struct.c_cc[VMIN] = 1;  // Set the minimum number of characters to read
        termios_struct.c_cc[VTIME] = 0; // No timeout for input
    }
    else
    {
        return E_NOK;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &termios_struct);  // Apply the new settings
    return E_OK;
}

// Function to map baudrate string to baudrate constant
speed_t get_baudrate(const char *baudrate_str) 
{
    if (strcmp(baudrate_str, "9600") == 0) 
        return B9600;
    else if (strcmp(baudrate_str, "19200") == 0) 
        return B19200;
    else if (strcmp(baudrate_str, "38400") == 0) 
        return B38400;
    else if (strcmp(baudrate_str, "57600") == 0) 
        return B57600;
    else if (strcmp(baudrate_str, "115200") == 0) 
        return B115200;
    else 
    {
        // If the baudrate is unsupported, print error and exit
        fprintf(stderr, "Unsupported baud rate: %s\n", baudrate_str);
        exit(1);
    }
}

// Function to configure UART settings (baud rate, data bits, stop bits, and parity)
int setup_uart(const char *device, speed_t baudrate) 
{
    uart_fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);  // Open UART device with read/write permissions
    if (uart_fd < 0) 
    {
        perror("Error opening UART");  // Print error if UART cannot be opened
        return E_NOK;
    }

    struct termios options;
    tcgetattr(uart_fd, &options);  // Get the current UART port settings

    cfsetispeed(&options, baudrate);  // Set the input baud rate
    cfsetospeed(&options, baudrate);  // Set the output baud rate

    // Set UART port settings for 8 data bits, no parity, and 1 stop bit
    options.c_cflag &= ~PARENB;  // No parity
    options.c_cflag &= ~CSTOPB;  // 1 stop bit
    options.c_cflag &= ~CSIZE;   // Clear the data bits size
    options.c_cflag |= CS8;      // 8 data bits

    // Disable canonical mode (line-buffered input), echoing, and signal generation
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    tcsetattr(uart_fd, TCSANOW, &options);  // Apply the configured UART settings
    return uart_fd;
}

// Function to write data to the UART device
int write_uart(const char *data) 
{
    int written_on_uart;
    pthread_mutex_lock(&uart_lock);                         // Lock the UART access to prevent race conditions
    written_on_uart = write(uart_fd, data, strlen(data));    // Write the data to UART
    pthread_mutex_unlock(&uart_lock);                       // Unlock UART access
    return written_on_uart;
}

// Function to continuously read data from the UART and display it to the user
void* read_uart(void* arg) 
{
    char buf[BUF_SIZE];  // Buffer to store incoming data
    while (1) 
    {
        int read_bits = read(uart_fd, buf, BUF_SIZE - 1);  // Read from UART
        if (read_bits > 0) 
        {
            buf[read_bits] = '\0';  // Null-terminate the received data

            if(OUT_FLAG == OUT_FLAG_SHELL)
            {
                // Delete previous input text and prepare the terminal for new received data
                delete_chars(21 + user_input_counter); // 21 = Enter text to send: 

                // Print received data to the terminal
                printf("\033[0;32mReceived:\033[0m %s\n", buf);
                fflush(stdout);  // Flush the output buffer to print immediately

                // Ask the user to enter text to send after displaying the received data
                printf("Enter text to send: ");
                if (user_input_counter) // if there any uncompleted transmit
                {
                    printf("%s", user_input);  // Show any partial input if the user started typing
                }
                fflush(stdout);  // Ensure immediate output
            }
            else if(OUT_FLAG == OUT_FLAG_DEST)
            {
                // write to destination
                int bytes_written = write(dest_fd, buf, read_bits);
                if (bytes_written != read_bits) 
                {
                    perror("Error writing to destination file\n");
                }
                else
                {
                    // Delete previous input text and prepare the terminal for new received data
                    delete_chars(21 + user_input_counter); // 21 = Enter text to send: 

                    // Print received data to the terminal
                    printf("\033[0;32mReceived:\033[0m saved %d to file.\n",bytes_written);
                    fflush(stdout);  // Flush the output buffer to print immediately

                    // Ask the user to enter text to send after displaying the received data
                    printf("Enter text to send: ");
                    if (user_input_counter) // if there any uncompleted transmit
                    {
                        printf("%s", user_input);  // Show any partial input if the user started typing
                    }
                    fflush(stdout);  // Ensure immediate output
                }
            }
        }
        else if (read_bits < 0) 
        {
            perror("Error reading from UART\n");  // Print error if reading from UART fails
        }
        usleep(1000);  // Sleep for 1ms to prevent CPU overuse
    }
    return E_OK;
}

// Function to continuously prompt the user for input and send it over UART
void* write_thread(void* arg) 
{
    while (1) 
    {
        // Display prompt for user input
        printf("Enter text to send: ");
        fflush(stdout);

        for(user_input_counter = 0; user_input_counter < BUF_SIZE - 1;) // get char by char
        {
            user_input[user_input_counter] = getchar();  // Get a character from the user
            // for fix input buffer ending from the previous data (read_uart function bug)
            user_input[user_input_counter + 1] = 0;  // Null-terminate the input string
            
            if(user_input[user_input_counter] == '\n')  // If user presses Enter
            {
                if(user_input_counter)  // If there's actual input (not an empty line)
                {
                    delete_chars(21 + user_input_counter);  // Delete previous input
                    user_input[user_input_counter] = 0;  // Remove the newline character

                    if(strncmp(user_input,"R>",2) == 0) // redirect recieved data to file
                    {
                        if(strcmp((const char *)&(user_input[2]),"shell") == 0)
                        {
                            printf("Redirection : to shell\n");
                            OUT_FLAG = OUT_FLAG_SHELL;
                        }
                        else
                        {
                            // Open the file for writing (create if it doesn't exist, truncate if it exists)
                            dest_fd = open((const char *)&(user_input[2]), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                            if (dest_fd == -1) 
                            {
                                perror("Error opening destination file\n");
                            }
                            else
                            {
                                printf("Redirection : to %s\n",&(user_input[2]));
                                OUT_FLAG = OUT_FLAG_DEST;
                            }
                        }
                    }
                    else if(strncmp(user_input,"T<",2) == 0) // redirect file to transmit
                    {
                            // Open the source file for reading
                            source_fd = open((const char *)&(user_input[2]), O_RDONLY);
                            if (source_fd == -1) 
                            {
                                perror("Error opening source file\n");
                            }
                            else
                            {
                                int bytes_read,bytes_written;
                                char read_buf[BUF_SIZE];
                                // Read from source and write to destination in chunks
                                while ((bytes_read = read(source_fd, read_buf, BUF_SIZE)) > 0) 
                                {
                                    // if(read_buf[bytes_read-1]== '\n')
                                    // {
                                    //     read_buf[bytes_read-1] = 0;
                                    // }
                                    bytes_written = write_uart(read_buf);;
                                    if (bytes_written != bytes_read) 
                                    {
                                        perror("Error writing to destination file\n");
                                    }
                                    else
                                    {
                                        printf("-%s-\n",read_buf);
                                        // Print the sent-> x bits transmited from y success
                                        printf("\033[0;31msent->\033[0m%d bits transmited from %s success\n",bytes_read, &(user_input[2]));
                                    }
                                }
                                close(source_fd);
                            }
                    }
                    else
                    {
                        printf("\033[0;31msent->\033[0m%s\n", user_input);  // Print the sent-> data
                        write_uart(user_input);  // Send the input data over UART
                    }

                    user_input_counter = 0;  // Reset the input counter
                    break;
                }
                else
                {
                    // If Enter was pressed without any input, do nothing
                }
            }
            else if(user_input[user_input_counter] == 127)  // Handle backspace (ASCII 127)
            {
                if(user_input_counter)
                {
                    user_input[user_input_counter - 1] = 0;     // Remove the last character from buf
                    delete_chars(1);                            // Delete the last character from the terminal
                    user_input_counter--;                       // Decrement the input counter
                }
            }
            // protect from arrow keys here (future update)
            else
            {
                printf("%c", user_input[user_input_counter]);   // Print the current character
                user_input_counter++;                           // Increment the input counter
            }
        }
    }
    return E_OK;
}

// Signal handler for SIGINT (Ctrl+C)kill-signal to cleanly exit
void sigint_handler(int sig) 
{
    delete_chars(21+user_input_counter);  // Clean up the terminal display before exiting
    printf("Trying to kill, ");
    cleanup_and_exit();  // Call cleanup and exit
}

// Function to clean up resources and exit the program gracefully
void cleanup_and_exit() 
{
    if(uart_fd >= 0) 
    {
        close(uart_fd);  // Close the UART file descriptor
    }

    if(dest_fd >= 0)
    {
        close(dest_fd);  // Close the dest file descriptor
    }

    set_input_mode(CANONICAL_MODE);

    pthread_mutex_destroy(&uart_lock);  // Destroy the mutex lock

    pthread_cancel(read_tid);   // Cancel the read thread
    pthread_cancel(write_tid);  // Cancel the write thread

    // Wait for threads to finish
    pthread_join(read_tid, NULL);
    pthread_join(write_tid, NULL);

    printf("successfully terminated\n");
    exit(E_OK);  // Exit the program
}
