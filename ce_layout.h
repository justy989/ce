#pragma once

#include "ce.h"

struct CeLayout_t;

typedef enum{
     CE_LAYOUT_TYPE_VIEW,
     CE_LAYOUT_TYPE_LIST,
     CE_LAYOUT_TYPE_TAB,
     CE_LAYOUT_TYPE_TAB_LIST,
}CeLayoutType_t;

typedef struct{
     struct CeLayout_t** layouts;
     int64_t layout_count;
     bool vertical;
     CeRect_t rect;
}CeLayoutList_t;

typedef struct{
     struct CeLayout_t* root;
     struct CeLayout_t* current;
     CeRect_t rect;
}CeTabLayout_t;

typedef struct{
     struct CeLayout_t** tabs;
     struct CeLayout_t* current;
     int64_t tab_count;
     CeRect_t rect;
}CeTabListLayout_t;

typedef struct CeLayout_t{
     CeLayoutType_t type;

     union{
          CeView_t view;
          CeLayoutList_t list;
          CeTabLayout_t tab;
          CeTabListLayout_t tab_list;
     };
}CeLayout_t;

CeLayout_t* ce_layout_tab_list_init(CeLayout_t* tab_layout);
CeLayout_t* ce_layout_tab_init(CeBuffer_t* buffer);
void ce_layout_free(CeLayout_t** layout);
bool ce_layout_split(CeLayout_t* layout, bool vertical);
void ce_layout_distribute_rect(CeLayout_t* layout, CeRect_t rect);
CeLayout_t* ce_layout_find_at(CeLayout_t* layout, CePoint_t point);
CeLayout_t* ce_layout_find_parent(CeLayout_t* root, CeLayout_t* node);
bool ce_layout_delete(CeLayout_t* root, CeLayout_t* node);
