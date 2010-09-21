/*************************************************************************
This file is part of dres the resource policy dependency resolver.

Copyright (C) 2010 Nokia Corporation.

This library is free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <glib-object.h>

#include "ohm-fact.h"

#define ACCESSORIES "com.nokia.policy.accessories"
#define DEVICE      "device"

#define fatal(ec, fmt, args...) do {	     \
    printf("fatal error: "fmt"\n", ## args); \
    exit(ec);				     \
  } while (0)



int
main(int argc, char *argv[])
{
  char *accessories[] = { "ihf", "earpiece", "microphone", NULL }, **acc;

  OhmFactStore     *store;
  OhmFact          *fact;
  OhmFactStoreView *view;
  OhmPatternMatch  *pm;
  GValue            dev, *d;
  GSList           *matches, *l;

  factmap_t        *map;
  char             *fields[] = { "device", NULL };

  g_type_init();

  if ((store = ohm_fact_store_new()) == NULL)
    fatal(1, "failed to create fact store");

  if ((view = ohm_fact_store_new_view(store, NULL)) == NULL)
    fatal(2, "failed to create view");
  
  if ((map = factmap_create(sotre, ACCESSORIES, fields)) == NULL)
    fatal(3, "failed to create factmap for %s", ACCESSORIES);
  
  factmap_dump(map);

  l = g_slist_prepend(NULL, ohm_pattern_new(ACCESSORIES));
  ohm_fact_store_view_set_interested(view, l);
  
  for (acc = accessories; *acc; acc++) {
    fact = ohm_fact_new(ACCESSORIES);
    dev  = ohm_value_from_string(*acc);
    ohm_fact_set(fact, DEVICE, &dev);
    if (!ohm_fact_store_insert(store, fact))
      fatal(2, "failed to add device %s to fact store", *acc);

    factmap_dump(map);
  }
  
  for (l = ohm_fact_store_get_facts_by_name(store, ACCESSORIES);
       l != NULL;
       l = g_slist_next(l)) {
    fact = (OhmFact *)l->data;
    d    = ohm_fact_get(fact, DEVICE);
    
    if (d == NULL)
      fatal(3, "failed to retrieve field %s for fact %p", DEVICE, fact);

    printf("got accessory %s\n", g_value_get_string(d));
  }

  matches = ohm_fact_store_change_set_get_matches(view->change_set);
  for (l = matches; l != NULL; l = g_slist_next(l)) {
    if (!OHM_PATTERN_IS_MATCH(l->data))
      fatal(4, "unexpected object type in changeset");
    pm = OHM_PATTERN_MATCH(l->data);
    printf("%s", ohm_pattern_match_to_string(pm));
  }

  if (matches == NULL) {
    printf("view had empty changeset\n");
  }


  return 0;
}
