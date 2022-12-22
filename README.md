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
$ ./main -c localhost 1234
```

A server emulates a data, render and io service that simply print the command.
A client picks a message at random and sends it to a random destination service on the server.

Sample output (server):
```
$ ./main -s 1234
(   0.000s) [main thread     ]               main.cxx:213   INFO| => Waiting for connection: localhost:1234
(   3.250s) [main thread     ]               main.cxx:216   INFO| => Client connected!
(   3.251s) [main thread     ]               main.cxx:172   INFO| Registered services.data:14752253762927134915
(   3.251s) [main thread     ]               main.cxx:172   INFO| Registered services.render:15335038379579459937
(   3.251s) [main thread     ]               main.cxx:172   INFO| Registered services.io:15497323717503977043
(   3.351s) [services.io     ]               main.cxx:155   INFO| read-3
(   3.451s) [services.io     ]               main.cxx:155   INFO| write-2
(   3.551s) [services.data   ]               main.cxx:155   INFO| update-pipeline
(   3.651s) [services.data   ]               main.cxx:155   INFO| update-state
(   3.751s) [services.data   ]               main.cxx:155   INFO| update-pipeline
(   3.852s) [services.render ]               main.cxx:155   INFO| render-3
(   3.952s) [services.data   ]               main.cxx:155   INFO| object-delete
(   4.052s) [services.io     ]               main.cxx:155   INFO| read
(   4.152s) [services.io     ]               main.cxx:155   INFO| read-4
(   4.252s) [services.data   ]               main.cxx:155   INFO| update-pipeline
(   4.352s) [services.data   ]               main.cxx:155   INFO| object-delete
(   4.452s) [services.io     ]               main.cxx:155   INFO| read-4
(   4.552s) [services.io     ]               main.cxx:155   INFO| write-2
(   4.653s) [services.io     ]               main.cxx:155   INFO| read-2
(   4.753s) [services.render ]               main.cxx:155   INFO| render
(   4.853s) [services.data   ]               main.cxx:155   INFO| update-state
(   4.953s) [services.data   ]               main.cxx:155   INFO| object-delete
(   5.053s) [services.data   ]               main.cxx:155   INFO| update-state
(   5.153s) [services.io     ]               main.cxx:155   INFO| write-2
(   6.152s) [main thread     ]               main.cxx:313   INFO| => Shutdown services..
(   6.152s) [services.io     ]               main.cxx:161   INFO| exit
(   6.152s) [services.data   ]               main.cxx:161   INFO| exit
(   6.152s) [services.render ]               main.cxx:161   INFO| exit
(   6.152s) [main thread     ]               main.cxx:319   INFO| => Shutdown comm..
(   6.152s) [comm.send       ]               main.cxx:89    INFO| exit
(   6.153s) [comm.recv       ]               main.cxx:126   INFO| exit
(   6.153s) [main thread     ]               main.cxx:327   INFO| Wait for client to disconnect..
(   6.253s) [main thread     ]               main.cxx:331   INFO| => Shutdown server..
```

Sample output (client):
```
$ ./main -c localhost 1234
(   0.000s) [main thread     ]               main.cxx:222   INFO| => Connected to localhost:1234
(   0.000s) [main thread     ]               main.cxx:244   INFO| =>Receiving service registry..
(   0.000s) [main thread     ]               main.cxx:258   INFO| => services.data:14752253762927134915
(   0.000s) [main thread     ]               main.cxx:258   INFO| => services.io:15497323717503977043
(   0.000s) [main thread     ]               main.cxx:258   INFO| => services.render:15335038379579459937
(   0.100s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: read-3
(   0.201s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: write-2
(   0.301s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: update-pipeline
(   0.401s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: update-state
(   0.501s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: update-pipeline
(   0.601s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: render-3
(   0.701s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: object-delete
(   0.801s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: read
(   0.901s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: read-4
(   1.002s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: update-pipeline
(   1.102s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: object-delete
(   1.202s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: read-4
(   1.302s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: write-2
(   1.402s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: read-2
(   1.502s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: render
(   1.602s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: update-state
(   1.702s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: object-delete
(   1.802s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: update-state
(   1.903s) [main thread     ]               main.cxx:70    INFO| => Enqueue msg: write-2
(   2.903s) [main thread     ]               main.cxx:313   INFO| => Shutdown services..
(   2.903s) [main thread     ]               main.cxx:319   INFO| => Shutdown comm..
(   2.903s) [comm.send       ]               main.cxx:89    INFO| exit
(   2.907s) [comm.recv       ]               main.cxx:126   INFO| exit
(   2.907s) [main thread     ]               main.cxx:334   INFO| => Shutdown client..
```
