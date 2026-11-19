/* Kiosque 32 : couches telnet (RFC 854) et WebSocket (RFC 6455, ws://)
   au-dessus d'un flux TCP. Indépendant du système : l'appelant fournit
   les octets reçus et une fonction d'écriture sur la connexion. */
#ifndef PROTO_H
#define PROTO_H

#define NET_TCP    0    /* octets bruts */
#define NET_TELNET 1    /* TCP avec négociation telnet (IAC) */
#define NET_WS     2    /* WebSocket non chiffré */

#define PROTO_HANDSHAKE 0   /* WebSocket : réponse HTTP attendue */
#define PROTO_OPEN      1
#define PROTO_REFUSED   2   /* le serveur a refusé le WebSocket */
#define PROTO_CLOSED    3   /* trame de fermeture reçue */

typedef void (*proto_sink_fn)(unsigned char c);
typedef void (*proto_out_fn)(const unsigned char *p, int n);

/* Prépare une connexion ; seed sert aux clés de masquage WebSocket. */
void proto_init(int kind, proto_sink_fn sink, proto_out_fn out, unsigned long seed);
/* WebSocket : envoie la requête GET d'ouverture (une fois connecté). */
void proto_ws_open(const char *host, unsigned int port, const char *path);
/* Octets reçus du serveur. */
void proto_feed(const unsigned char *p, int n);
/* Octets à envoyer au serveur (frappe, réponse du terminal). */
void proto_send(const unsigned char *p, int n);
/* Fermeture propre (trame de fermeture en WebSocket). */
void proto_close(void);
extern int proto_state;

#endif
