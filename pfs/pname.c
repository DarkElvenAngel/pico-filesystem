// pname.c - Code for manipulating path names
// Copyright (c) 2023, Memotech-Bill
// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pname.h>
#include <pfs_private.h>

#ifndef STATIC
#define STATIC  static
#endif

typedef struct s_pname
    {
    struct s_pname *    prev;
    struct s_pname *    next;
    const char *        name;
    int                 len;
    } PNAME;

STATIC char rootdir = '/';

STATIC void pname_clean (PNAME *pn)
    {
    PNAME *pe1 = pn->next;
    PNAME *pe2;
    while (( pe1 != pn ) && ( pe1 != NULL ))
        {
        pe2 = pe1->next;
        pfs_free (pe1);
        pe1 = pe2;
        }
    pn->prev = pn;
    pn->next = pn;
    }

STATIC void pname_delete (PNAME *pn)
    {
    pname_clean (pn);
    pfs_free (pn);
    }

STATIC void pname_link (PNAME *pn, PNAME *pe)
    {
    pe->prev = pn->prev;
    pe->next = pn;
    pn->prev->next = pe;
    pn->prev = pe;
    }

STATIC PNAME * pname_unlink (PNAME *pe)
    {
    PNAME *pep = pe->prev;
    PNAME *pen = pe->next;
    pep->next = pen;
    pen->prev = pep;
    pe->prev = NULL;
    pe->next = NULL;
    return pe;
    }

STATIC bool pname_scan (PNAME *pn)
    {
    const char * pend = pn->name + pn->len;
    const char * ps1 = pn->name;
    if (( *ps1 == '/' ) || ( *ps1 == '\\' ))
        {
        PNAME *pe = (PNAME *) pfs_malloc (sizeof (PNAME));
        if ( pe == NULL ) return false;
        pe->name = &rootdir;
        pe->len = 1;
        pname_link (pn, pe);
        ++ps1;
        }
    while ( true )
        {
        while (( *ps1 == '/' ) || ( *ps1 == '\\' )) ++ps1;
        if ( ps1 >= pend ) break;
        const char *ps2 = ps1;
        while (( *ps2 != '/' ) && ( *ps2 != '\\' ) && ( ps2 < pend )) ++ps2;
        if (( ps2 > ps1 + 1 ) || ( *ps1 != '.' ))
            {
            PNAME *pe = (PNAME *) pfs_malloc (sizeof (PNAME));
            if ( pe == NULL ) return false;
            pe->name = ps1;
            pe->len = ps2 - ps1;
            pname_link (pn, pe);
            }
        ps1 = ps2 + 1;
        }
    return true;
    }

STATIC PNAME * pname_create (const char *psPath)
    {
    PNAME *pn = (PNAME *) pfs_malloc (sizeof (PNAME));
    if ( pn == NULL ) return NULL;
    pn->prev = pn;
    pn->next = pn;
    pn->name = psPath;
    pn->len = strlen (psPath);
    if ( ! pname_scan (pn) )
        {
        pname_delete (pn);
        return NULL;
        }
    return pn;
    }

STATIC void pname_join (PNAME *pn1, PNAME *pn2)
    {
    while (( pn2->next != pn2 ) && ( pn2->next != NULL ))
        {
        PNAME *pe2 = pn2->next;
        pname_unlink (pe2);
        if (( pe2->len == 2 ) && ( pe2->name[0] == '.' ) && ( pe2->name[1] == '.' ))
            {
            PNAME *pe1 = pn1->prev;
            if (( pe1 != pn1 ) && ( pe1->name[0] != '/' ))
                {
                pname_unlink (pe1);
                pfs_free (pe1);
                }
            pfs_free (pe2);
            }
        else
            {
            if ( pe2->name[0] == '/' ) pname_clean (pn1);
            pname_link (pn1, pe2);
            }
        }
    }

char * pname_mkname (PNAME *pn)
    {
    int nlen = 0;
    PNAME *pe = pn->next;
    while (( pe != pn ) && ( pe != NULL ))
        {
        nlen += pe->len + 1;
        pe = pe->next;
        }
    char *psName = (char *) pfs_malloc (nlen + 1);
    if ( psName == NULL ) return NULL;
    char *ps = psName;
    pe = pn->next;
    while (( pe != pn ) && ( pe != NULL ))
        {
        if ( pe->name[0] != '/' )
            {
            *ps = '/';
            ++ps;
            strncpy (ps, pe->name, pe->len);
            ps += pe->len;
            }
        pe = pe->next;
        }
    if ( ps == psName )
        {
        *ps = '/';
        ++ps;
        }
    *ps = '\0';
    return psName;
    }

char * pname_append (const char *psPath1, const char *psPath2)
    {
    PNAME *pn1 = pname_create (psPath1);
    if ( pn1 == NULL ) return NULL;
    PNAME *pn2 = pname_create (psPath2);
    if ( pn2 == NULL )
        {
        pname_delete (pn1);
        return NULL;
        }
    pname_join (pn1, pn2);
    char *ps = pname_mkname (pn1);
    pname_delete (pn1);
    pname_delete (pn2);
    return ps;
    }
