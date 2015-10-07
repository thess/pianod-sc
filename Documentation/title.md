# pianod Documentation
`pianod` is a Pandora music service client as a UNIX daemon.  `pianod` uses the football socket library to expose both IPv4 and IPv6 sockets for control, which can be done via `nc`(1), `telnet`(1), or other connection.  Football also provides support for HTTP and Websocket protocols.

The package includes `piano`, a shell script to access and control the music server.  This allows control from other shell scripts, and provides a reference implementation for communicating with the daemon.

This documentation is include with pianod in Markdown format.  It is also available in [HTML].

[HTML]: http://deviousfish.com/pianod/pianod.html
