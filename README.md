# Application server
### _A client-server application capable of handling requests of varying loads._

## Compile
       make 

## Run (client)

       Usage: c <-t nsecs> <fifoname>


`nsecs` : The program will run for nsecs seconds.  
`fifoname` : The name of the public comunication channel used by the client to send requests to
the server.

## Run (server)

       Usage: s <-t nsecs> [-l bufsz] <fifoname>

`nsecs` : The program will run for nsecs seconds.  
`bufsz` : Size of the buffer used to store the requests' results.  
`fifoname` : The name of the public comunication channel used to receive requests.

## Notes



## Authorship / Participation

| Name                     | Email                      | Participation  |
|:-------------------------|:---------------------------|:--------------:|
| José Frederico Rodrigues | <up201807626@edu.fe.up.pt> | 33%            |
| José Pedro Ferreira      | <up201904515@fe.up.pt>     | 33%            |
| Lucas Calvet Santos      | <up201904517@fe.up.pt>     | 34%            |
