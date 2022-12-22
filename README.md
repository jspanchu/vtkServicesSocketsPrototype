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
A client picks a message at random and sends it to a random destination service on the server.

Sample output (server):
```
$ ./main -s 1234
(   0.000s) [main thread     ]               main.cxx:233   INFO| => Waiting for connection: localhost:1234

(   2.528s) [main thread     ]               main.cxx:236   INFO| => Client connected!
(   2.528s) [main thread     ]               main.cxx:187   INFO| => Registered services.data:14752253762927134915
(   2.528s) [main thread     ]               main.cxx:187   INFO| => Registered services.render:15335038379579459937
(   2.528s) [main thread     ]               main.cxx:187   INFO| => Registered services.io:15497323717503977043
(   2.528s) [main thread     ]               main.cxx:252   INFO| => Advertise service registry
(   2.539s) [services.render ]               main.cxx:170   INFO| render-4
(   2.539s) [services.io     ]               main.cxx:170   INFO| write-2
(   2.539s) [services.render ]               main.cxx:170   INFO| render-3
(   2.539s) [services.render ]               main.cxx:170   INFO| render-3
(   2.539s) [services.data   ]               main.cxx:170   INFO| object-delete
(   2.539s) [services.io     ]               main.cxx:170   INFO| read
(   2.539s) [services.render ]               main.cxx:170   INFO| render
(   2.539s) [services.data   ]               main.cxx:170   INFO| update-state
(   2.539s) [services.data   ]               main.cxx:170   INFO| object-delete
(   2.539s) [services.render ]               main.cxx:170   INFO| render-4
(   2.539s) [services.data   ]               main.cxx:170   INFO| object-delete
(   2.539s) [services.data   ]               main.cxx:170   INFO| object-delete
(   2.539s) [services.render ]               main.cxx:170   INFO| render-4
(   2.539s) [services.render ]               main.cxx:170   INFO| render
(   2.539s) [services.render ]               main.cxx:170   INFO| render-2
(   2.539s) [services.render ]               main.cxx:170   INFO| render
(   2.539s) [services.io     ]               main.cxx:170   INFO| read-2
(   2.539s) [services.data   ]               main.cxx:170   INFO| update-state
(   2.539s) [services.render ]               main.cxx:170   INFO| render-2
(   2.539s) [services.io     ]               main.cxx:170   INFO| write-2
(   3.539s) [main thread     ]               main.cxx:343   INFO| => Shutdown services
(   3.539s) [services.io     ]               main.cxx:176   INFO| exit
(   3.539s) [services.data   ]               main.cxx:176   INFO| exit
(   3.539s) [services.render ]               main.cxx:176   INFO| exit
(   3.539s) [main thread     ]               main.cxx:349   INFO| => Shutdown comm
(   3.539s) [comm.send       ]               main.cxx:96    INFO| exit
(   3.539s) [comm.recv       ]               main.cxx:141   INFO| exit
(   3.539s) [main thread     ]               main.cxx:357   INFO| Wait for client to disconnect
(   3.640s) [main thread     ]               main.cxx:361   INFO| => Shutdown server
```

Sample output (client):
```
$ ./main -c localhost 1234 -n20
(   0.000s) [main thread     ]               main.cxx:242   INFO| => Connected to localhost:1234
(   0.000s) [main thread     ]               main.cxx:265   INFO| => Receiving service registry
(   0.000s) [main thread     ]               main.cxx:279   INFO| => services.data:14752253762927134915
(   0.000s) [main thread     ]               main.cxx:279   INFO| => services.io:15497323717503977043
(   0.000s) [main thread     ]               main.cxx:279   INFO| => services.render:15335038379579459937
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render-4
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: write-2
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render-3
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render-3
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: object-delete
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: read
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: update-state
(   0.010s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: object-delete
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render-4
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: object-delete
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: object-delete
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render-4
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render-2
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: read-2
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: update-state
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: render-2
(   0.011s) [main thread     ]               main.cxx:73    INFO| => Enqueue msg: write-2
(   1.011s) [main thread     ]               main.cxx:343   INFO| => Shutdown services
(   1.011s) [main thread     ]               main.cxx:349   INFO| => Shutdown comm
(   1.011s) [comm.send       ]               main.cxx:96    INFO| exit
(   1.012s) [comm.recv       ]               main.cxx:141   INFO| exit
(   1.012s) [main thread     ]               main.cxx:364   INFO| => Shutdown client
```
