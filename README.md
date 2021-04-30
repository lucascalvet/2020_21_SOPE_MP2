# Application Server
### _A client-server application capable of handling requests of varying loads._

## Compile
### Client

       make c

### Server

       make s  

## Run
### Client

       Usage: c <-t nsecs> <fifoname>

`nsecs` : The number of seconds the program shall run for (approximately).  
`fifoname` : The name (absolute or relative) of the public comunication channel (FIFO) used by the client to send requests to the server.

### Server

       Usage: s <-t nsecs> [-l bufsz] <fifoname>

`nsecs` : The number of seconds the program shall run for (approximately).  
`bufsz` : Size of the buffer used to store the requests' results.  
`fifoname` : The name (absolute or relative) of the public comunication channel (FIFO) used by the client to send requests to the server.

## Notes

- A `fifoname` containing spaces must be enclosed in quotes or escaped with a backslash for each blank space.

- If a request thread raises an unexpected error, we ignore the request and terminate said thread.

- For error handling, we use the `error()` function to print to `stderr` a description of the error.

- For testing with the shell script, run `./script.sh <test_no>` with `test_no` between 1 and 3 (`./script.sh clean` for cleaning the log files).


## Authorship / Participation

| Name                     | Email                          | Participation  |
|:-------------------------|:-------------------------------|:--------------:|
| José Frederico Rodrigues | <up201807626@edu.fe.up.pt>     | 33%            |
| José Pedro Ferreira      | <up201904515@edu.fe.up.pt>     | 33%            |
| Lucas Calvet Santos      | <up201904517@edu.fe.up.pt>     | 34%            |
