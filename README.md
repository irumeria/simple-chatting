# Simple Chatting

## Description
+ simple unix server-client program for chating, written in C

## Usage

### Server

```bash
gcc ./src/block/server.c  -o ./dist/server
./dist/server
```

### Client

```bash
gcc ./src/block/client.c  -o ./dist/client
./dist/client
```

### Client Operation

+ sign up : 
```bash
sign $username
```

+ send message : 
```bash
send $other_user $message
```
+ the message sent by other user will not display automatically, you should pull it from server manually
```bash
pull
```

+ The server stores user info in memory, it will disappear if the server is stoped.