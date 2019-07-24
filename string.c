
/* string.c */

void *memset(void *s, int v, int n)
{
    char *p = (char *)s;

    while(1){
        if(--n<0)
            break;
        *p++ = (char)v;
    }

    return s;
}

void *memcpy(void *to, void *from, int n)
{
    char *t = (char *)to;

    while(1){
        if(--n<0)
            break;
        *t++ = *(char*)from++;
    }

    return to;
}

char *strcpy(char *dst, char *src)
{
    char *d = dst;
    char t;

    do{
        t = *src++;
        *d++ = t;
    }while(t);

    return dst;
}

char *strncpy(char *dst, char *src, int n)
{
    char *d = dst;
    char t;

    do{
        if(--n<0)
            break;
        t = *src++;
        *d++ = t;
    }while(t);

    return dst;
}

char *strchr(char *src, char ch)
{
    char t;

	while(1){
        t = *src;
		if(t==0)
			break;
        if(t==ch)
			return src;
		src++;
    };

    return (void*)0;
}

int strcmp(char *s1, char *s2)
{
    int r;
    int t;

    while(1){
        t = (int)*s1++;
        r = t - (int)*s2++;
        if(r)
            break;
        if(t==0)
            break;
    }

    return r;
}

int toupper(char c)
{
	if(c>='a' && c<='z')
		c = c-'a'+'A';
	return c;
}

int tolower(char c)
{
	if(c>='A' && c<='Z')
		c = c-'A'+'a';
	return c;
}


int strcasecmp(char *s1, char *s2)
{
    int c1, c2;

    do{
        c1 = (int)*s1++;
        c2 = (int)*s2++;
		c1 = tolower(c1);
		c2 = tolower(c2);
	}while(c1 && c2 && (c1==c2));

    return c1-c2;
}

int strncasecmp(char *s1, char *s2, int n)
{
    int c1, c2;

	if(n==0)
		return 0;

    do{
        c1 = (int)*s1++;
        c2 = (int)*s2++;
		c1 = tolower(c1);
		c2 = tolower(c2);
		n -= 1;
	}while(n && c1 && c2 && (c1==c2));

    return c1-c2;
}

int strncmp(char *s1, char *s2, int n)
{
    register int r = 0;
    register int t;

    while(1){
        if(--n<0)
            break;
        t = (int)*s1++;
        r = t - (int)*s2++;
        if(r)
            break;
        if(t==0)
            break;
    }

    return r;
}

int strlen(char *s)
{
    register char *p = s;

    while(*p++);

    return p-s-1;
}


unsigned int strtoul(char *str, char **endptr, int requestedbase, int *ret)
{
	unsigned int num = 0;
	int c, digit, retv;
	int base, nchars, leadingZero;

	base = 10;
	nchars = 0;

	if(str==0 || *str==0){
		retv = 2;
		goto exit;
	}
	retv = 0;

	if(requestedbase)
		base = requestedbase;

	while ((c = *str) != 0) {
		if (nchars == 0 && c == '0') {
			leadingZero = 1;
			goto step;
		} else if (leadingZero && nchars == 1) {
			if (c == 'x') {
				base = 16;
				goto step;
			} else if (c == 'o') {
				base = 8;
				goto step;
			}
		}
		if (c >= '0' && c <= '9') {
			digit = c - '0';
		} else if (c >= 'a' && c <= 'z') {
			digit = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'Z') {
			digit = c - 'A' + 10;
		} else {
			retv = 3;
			break;
		}
		if (digit >= base) {
			retv = 4;
			break;
		}
		num *= base;
		num += digit;
step:
		str++;
		nchars++;
	}

exit:
	if(ret)
		*ret = retv;
	if(endptr)
		*endptr = str;

	return num;
}

