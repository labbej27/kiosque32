/* Kiosque 32 : fonctions de chaîne pour la version Windows, qui n'est liée
   à aucune bibliothèque C (seulement kernel32, user32, gdi32 et wsock32,
   présentes depuis Windows 95). Compilé avec -fno-builtin et
   -fno-tree-loop-distribute-patterns pour que GCC ne remplace pas ces
   boucles par un appel à elles-mêmes. */
#include <stddef.h>

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p < q) {
        while (n--) *p++ = *q++;
    } else {
        p += n; q += n;
        while (n--) *--p = *--q;
    }
    return d;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

char *strcpy(char *d, const char *s)
{
    char *p = d;
    while ((*p++ = *s++) != 0) ;
    return d;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}
