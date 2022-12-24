Investigate usage of `vtkSocket` API to send and receive messages asynchronously.

## Build

```
$ export RXCPP_DIR=/path/to/rxcpp/source
$ export VTK_DIR=/path/to/vtk/build
$ mkdir build && cd build && cmake -GNinja -S ../ -B .
$ ninja
```

## Run

Launch in server mode.
```
$ ./main -s 1234
```

Launch in client mode and connect to the server we created above.
```
$ ./main -c localhost 1234 -n20
```
Here, the `-n` argument specifies no. of remote commands to issue.

In server mode, three services are emulated: a data, render and io service that simply print the commands.
The communication is bi-directional. A client picks a message at random and sends it to a random destination
service on the server. The server sends back a response on the same socket.

Sample output (server):
```bash
$ .\main.exe -s 1234
(   0.001s) [main thread     ]               main.cxx:299   INFO| => Waiting for connection: localhost:1234
(   9.431s) [main thread     ]               main.cxx:301   INFO| => Client connected!
(   9.431s) [main thread     ]               main.cxx:246   INFO| => Registered services.data:9609368131611085317
(   9.431s) [main thread     ]               main.cxx:246   INFO| => Registered services.render:4124982108956774541
(   9.432s) [main thread     ]               main.cxx:246   INFO| => Registered services.io:628033378311753073
(   9.432s) [main thread     ]               main.cxx:316   INFO| => Advertise service registry
(   9.433s) [services.io     ]               main.cxx:227   INFO| write-2
(   9.433s) [services.render ]               main.cxx:227   INFO| render
(   9.433s) [services.io     ]               main.cxx:134   INFO| => Enqueue msg: response-write-2
(   9.433s) [services.render ]               main.cxx:134   INFO| => Enqueue msg: response-render
(   9.433s) [services.io     ]               main.cxx:227   INFO| read-2
(   9.433s) [services.render ]               main.cxx:227   INFO| render-4
(   9.433s) [services.io     ]               main.cxx:134   INFO| => Enqueue msg: response-read-2
(   9.433s) [services.render ]               main.cxx:134   INFO| => Enqueue msg: response-render-4
(   9.433s) [services.render ]               main.cxx:227   INFO| render
(   9.433s) [services.io     ]               main.cxx:227   INFO| read-4
(   9.433s) [services.render ]               main.cxx:134   INFO| => Enqueue msg: response-render
(   9.434s) [services.io     ]               main.cxx:134   INFO| => Enqueue msg: response-read-4
(   9.434s) [services.data   ]               main.cxx:227   INFO| update-state
(   9.434s) [services.data   ]               main.cxx:134   INFO| => Enqueue msg: response-update-state
(   9.434s) [services.render ]               main.cxx:227   INFO| render
(   9.434s) [services.render ]               main.cxx:134   INFO| => Enqueue msg: response-render
(   9.434s) [services.io     ]               main.cxx:227   INFO| read-2
(   9.434s) [services.render ]               main.cxx:227   INFO| render-2
(   9.434s) [services.io     ]               main.cxx:134   INFO| => Enqueue msg: response-read-2
(   9.434s) [services.render ]               main.cxx:134   INFO| => Enqueue msg: response-render-2
(   9.434s) [services.render ]               main.cxx:227   INFO| render-3
(   9.434s) [services.data   ]               main.cxx:227   INFO| update-state
(   9.434s) [services.render ]               main.cxx:134   INFO| => Enqueue msg: response-render-3
(   9.434s) [services.data   ]               main.cxx:134   INFO| => Enqueue msg: response-update-state
(   9.434s) [services.data   ]               main.cxx:227   INFO| update-state
(   9.434s) [services.data   ]               main.cxx:134   INFO| => Enqueue msg: response-update-state
(   9.434s) [services.data   ]               main.cxx:227   INFO| object-delete
(   9.434s) [services.data   ]               main.cxx:134   INFO| => Enqueue msg: response-object-delete
(   9.434s) [services.data   ]               main.cxx:227   INFO| object-delete
(   9.434s) [services.data   ]               main.cxx:134   INFO| => Enqueue msg: response-object-delete
(  10.444s) [services.data   ]               main.cxx:233   INFO| exit
(  10.444s) [services.render ]               main.cxx:233   INFO| exit
(  10.444s) [comm.recv       ]               main.cxx:191   INFO| exit
(  10.444s) [services.io     ]               main.cxx:233   INFO| exit
(  10.444s) [main thread     ]               main.cxx:435   INFO| Average load 1.02
(  10.444s) [main thread     ]               main.cxx:438   INFO| => Unsubscribe services
(  10.444s) [main thread     ]               main.cxx:455   INFO| Wait for client to disconnect
(  10.555s) [main thread     ]               main.cxx:460   INFO| => Shutdown receiver
(  10.555s) [main thread     ]               main.cxx:464   INFO| => Shutdown server
```

Sample output (client):
```bash
$ .\main.exe -c localhost 1234 -n15
(   0.005s) [main thread     ]               main.cxx:307   INFO| => Connected to localhost:1234
(   0.005s) [main thread     ]               main.cxx:329   INFO| => Receiving service registry
(   0.005s) [main thread     ]               main.cxx:343   INFO| => services.data:9609368131611085317
(   0.005s) [main thread     ]               main.cxx:343   INFO| => services.io:628033378311753073
(   0.005s) [main thread     ]               main.cxx:343   INFO| => services.render:4124982108956774541
(   0.006s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: write-2
(   0.006s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: render
(   0.006s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: read-2
(   0.006s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: render-4
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: render
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: read-4
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-write-2
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: update-state
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-render
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: render
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-read-2
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: render-2
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-render-4
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: read-2
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-render
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: render-3
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-read-4
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: update-state
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-update-state
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: update-state
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-render
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: object-delete
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-read-2
(   0.007s) [main thread     ]               main.cxx:134   INFO| => Enqueue msg: object-delete
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-render-2
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-render-3
(   0.007s) [response        ]               main.cxx:401   INFO| reply: response-update-state
(   0.008s) [response        ]               main.cxx:401   INFO| reply: response-update-state
(   0.008s) [response        ]               main.cxx:401   INFO| reply: response-object-delete
(   0.008s) [response        ]               main.cxx:401   INFO| reply: response-object-delete
(   1.017s) [main thread     ]               main.cxx:419   INFO| Average load 1.34
(   1.017s) [main thread     ]               main.cxx:438   INFO| => Unsubscribe services
(   1.017s) [main thread     ]               main.cxx:467   INFO| => Shutdown client
(   1.017s) [main thread     ]               main.cxx:470   INFO| => Shutdown receiver
(   1.128s) [comm.recv       ]               main.cxx:191   INFO| exit
```
