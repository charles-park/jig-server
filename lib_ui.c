//------------------------------------------------------------------------------
/**
 * @file lib_ui.c
 * @author charles-park (charles.park@hardkernel.com)
 * @brief User interface library (include parser)
 * @version 0.1
 * @date 2022-05-10
 * 
 * @copyright Copyright (c) 2022
 * 
 */
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <getopt.h>

#include "lib_ui.h"

//------------------------------------------------------------------------------
// Function prototype.
//------------------------------------------------------------------------------
static   r_item_t    *_ui_find_r_item  (ui_grp_t *ui_grp, int *sid, int fid);
static   s_item_t    *_ui_find_s_item  (ui_grp_t *ui_grp, int *sid, int fid);

static   int         _my_strlen        (char *str);
static   int         _ui_str_scale     (int w, int h, int lw, int slen);
static   void        _ui_str_pos_xy    (r_item_t *r_item, s_item_t *s_item);
static   void        _ui_clr_str       (fb_info_t *fb, r_item_t *r_item, s_item_t *s_item);
static   void        _ui_update_r      (fb_info_t *fb, r_item_t *r_item);
static   void        _ui_update_s      (fb_info_t *fb, s_item_t *s_item, int x, int y);
static   void        _ui_update_extra  (fb_info_t *fb, ui_grp_t *ui_grp, int id);
static   void        _ui_update        (fb_info_t *fb, ui_grp_t *ui_grp, int id);
static   void        _ui_parser_cmd_C  (char *buf, fb_info_t *fb, ui_grp_t *ui_grp);
static   void        _ui_parser_cmd_R  (char *buf, fb_info_t *fb, ui_grp_t *ui_grp);
static   void        _ui_parser_cmd_S  (char *buf, fb_info_t *fb, ui_grp_t *ui_grp);
static   void        _ui_parser_cmd_G  (char *buf, fb_info_t *fb, ui_grp_t *ui_grp);

         void        ui_set_ritem      (fb_info_t *fb, ui_grp_t *ui_grp,
                                          int f_id, int bc, int lc);
         void        ui_set_sitem      (fb_info_t *fb, ui_grp_t *ui_grp,
                                          int id, int fc, int bc, char *str);
         void        ui_set_str        (fb_info_t *fb, ui_grp_t *ui_grp,
                                 int id, int x, int y, int scale, int font, char *fmt, ...);
         void        ui_set_printf     (fb_info_t *fb, ui_grp_t *ui_grp,
                                 int id, char *fmt, ...);
         void        ui_update         (fb_info_t *fb, ui_grp_t *ui_grp, int id);
         void        ui_close          (ui_grp_t *ui_grp);
         ui_grp_t    *ui_init          (fb_info_t *fb, const char *cfg_filename);

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

/*
   UI Config file 형식

   [ type ]
   '#' : commant
   'R' : Rect data
   'S' : string data
   'C' : default config data
   'L' : Line data
   'G' : Rect group data

   Rect data x, y, w, h는 fb의 비율값 (0%~100%), 모든 컬러값은 32bits rgb data.

   ui.cfg file 참조
*/

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
static r_item_t *_ui_find_r_item (ui_grp_t *ui_grp, int *sid, int fid)
{
   int i;
   /*
      여러개의 같은 아이디가 있을 수 있으므로 아래와 같이 검색한다.
      *sid = 시작 아이디
      fid  = 찾을 아이디
   */
   for (i = *sid; i < ui_grp->r_cnt; i++) {
      if (fid == ui_grp->r_item[i].id) {
         *sid = i + 1;
         return &ui_grp->r_item[i];
      }
   }
   return NULL;
}

//------------------------------------------------------------------------------
static s_item_t *_ui_find_s_item (ui_grp_t *ui_grp, int *sid, int fid)
{
   int i;
   /*
      여러개의 같은 아이디가 있을 수 있으므로 아래와 같이 검색한다.
      *sid = 시작 아이디
      fid  = 찾을 아이디
   */
   for (i = *sid; i < ui_grp->s_cnt; i++) {
      if (fid == ui_grp->s_item[i].r_id) {
         *sid = i + 1;
         return &ui_grp->s_item[i];
      }
   }
   return NULL;
}

//------------------------------------------------------------------------------
static int _my_strlen(char *str)
{
   int cnt = 0, err = 512;

   /* utf-8 에서 한글표현은 3바이트 */
   while ((*str != 0x00) && err--) {
      if (*str & 0x80) {
         str += 3;   cnt += 2;
      } else {
         str += 1;   cnt++;
      }
   }
   return err ? cnt : 0;
}

//------------------------------------------------------------------------------
static int _ui_str_scale (int w, int h, int lw, int slen)
{
   int as, w_len, h_len;

   /* auto scaling */
   /* 배율이 설정되어진 최대치 보다 큰 경우 종료한다. */
   for (as = 1; as < ITEM_SCALE_MAX; as++) {
      w_len = FONT_ASCII_WIDTH * as * slen + lw * 2;
      h_len = FONT_HEIGHT      * as        + lw * 2;
      /*
         만약 배율이 1인 경우에도 화면에 표시되지 않는 경우 scale은 0값이 되고
         문자열은 화면상의 표시가 되지 않는다.
      */
      if ((w_len > w) || (h_len > h)) {
         if (as == 1)
            err("String length too big. String can't display(scale = 0).\n");
         return (as -1);
      }
   }
   return ITEM_SCALE_MAX;
}

//------------------------------------------------------------------------------
static void _ui_str_pos_xy (r_item_t *r_item, s_item_t *s_item)
{
   int slen = _my_strlen(s_item->str);

   if (s_item->x < 0) {
      slen = slen * FONT_ASCII_WIDTH * s_item->scale;
      s_item->x = ((r_item->w - slen) / 2);
   }
   if (s_item->y < 0)
      s_item->y = ((r_item->h - FONT_HEIGHT * s_item->scale)) / 2;
}

//------------------------------------------------------------------------------
static void _ui_clr_str (fb_info_t *fb, r_item_t *r_item, s_item_t *s_item)
{
   int color = s_item->fc.uint;

   /* 기존 String을 배경색으로 다시 그림(텍스트 지움) */
   /* string x, y 좌표 연산 */
   s_item->fc.uint = s_item->bc.uint;
   _ui_str_pos_xy(r_item, s_item);
   _ui_update_s (fb, s_item, r_item->x, r_item->y);
   s_item->fc.uint = color;
   memset (s_item->str, 0x00, ITEM_STR_MAX);
}

//------------------------------------------------------------------------------
static void _ui_update_r (fb_info_t *fb, r_item_t *r_item)
{
   draw_fill_rect (fb, r_item->x, r_item->y, r_item->w, r_item->h,
                     r_item->bc.uint);
   if (r_item->lw)
      draw_rect (fb, r_item->x, r_item->y, r_item->w, r_item->h, r_item->lw,
                     r_item->lc.uint);
}

//------------------------------------------------------------------------------
static void _ui_update_s (fb_info_t *fb, s_item_t *s_item, int x, int y)
{
   draw_text (fb, x + s_item->x, y + s_item->y, s_item->fc.uint, s_item->bc.uint,
               s_item->scale, s_item->str);
}

//------------------------------------------------------------------------------
static void _ui_update_extra (fb_info_t *fb, ui_grp_t *ui_grp, int id)
{
   // extra item update
   int i;
   for (i = 0; i < ui_grp->r_cnt; i++)
      if (id == ui_grp->r_item[i].id)
         _ui_update_r (fb, &ui_grp->r_item[i]);

   for (i = 0; i < ui_grp->s_cnt; i++)
      if (id == ui_grp->s_item[i].r_id)
         _ui_update_s (fb, &ui_grp->s_item[i], 0, 0);
}

//------------------------------------------------------------------------------
static void _ui_update (fb_info_t *fb, ui_grp_t *ui_grp, int id)
{
   int n_rid = 0, n_sid = 0;

   r_item_t *r_item;
   s_item_t *s_item;

   if (id < ITEM_COUNT_MAX) {
      while ((r_item = _ui_find_r_item(ui_grp, &n_rid, id)) != NULL) {

         _ui_update_r (fb, r_item);

         n_sid = 0;
         while ((s_item = _ui_find_s_item(ui_grp, &n_sid, id)) != NULL) {
            if (s_item->f_type < 0)
               s_item->f_type = ui_grp->f_type;

            if ((signed)s_item->bc.uint < 0)
               s_item->bc.uint = r_item->bc.uint;

            set_font(s_item->f_type);

            if (s_item->scale < 0)
               s_item->scale = _ui_str_scale (r_item->w, r_item->h, r_item->lw,
                                             _my_strlen(s_item->str));
            _ui_str_pos_xy(r_item, s_item);
            _ui_update_s (fb, s_item, r_item->x, r_item->y);
         }
      }
   }
   else
      _ui_update_extra (fb, ui_grp, id);
}

//------------------------------------------------------------------------------
static void _ui_parser_cmd_C (char *buf, fb_info_t *fb, ui_grp_t *ui_grp)
{
   char *ptr = strtok (buf, ",");

   ptr = strtok (NULL, ",");     fb->is_bgr        = (atoi(ptr) != 0) ? 1: 0;
   ptr = strtok (NULL, ",");     ui_grp->fc.uint   = strtol(ptr, NULL, 16);
   ptr = strtok (NULL, ",");     ui_grp->bc.uint   = strtol(ptr, NULL, 16);
   ptr = strtok (NULL, ",");     ui_grp->lc.uint   = strtol(ptr, NULL, 16);
   ptr = strtok (NULL, ",");     ui_grp->f_type    = atoi(ptr);

   set_font(ui_grp->f_type);
}

//------------------------------------------------------------------------------
static void _ui_parser_cmd_R (char *buf, fb_info_t *fb, ui_grp_t *ui_grp)
{
   int r_cnt = ui_grp->r_cnt;
   char *ptr = strtok (buf, ",");

   ptr = strtok (NULL, ",");     ui_grp->r_item[r_cnt].id   = atoi(ptr);
   ptr = strtok (NULL, ",");     ui_grp->r_item[r_cnt].x    = atoi(ptr);
   ptr = strtok (NULL, ",");     ui_grp->r_item[r_cnt].y    = atoi(ptr);
   ptr = strtok (NULL, ",");     ui_grp->r_item[r_cnt].w    = atoi(ptr);
   ptr = strtok (NULL, ",");     ui_grp->r_item[r_cnt].h    = atoi(ptr);
   ptr = strtok (NULL, ",");

   ui_grp->r_item[r_cnt].bc.uint  = strtol(ptr, NULL, 16);
   ptr = strtok (NULL, ",");     ui_grp->r_item[r_cnt].lw   = atoi(ptr);

   ptr = strtok (NULL, ",");
   ui_grp->r_item[r_cnt].lc.uint = strtol(ptr, NULL, 16);

   ui_grp->r_item[r_cnt].x = (ui_grp->r_item[r_cnt].x * fb->w / 100);
   ui_grp->r_item[r_cnt].y = (ui_grp->r_item[r_cnt].y * fb->h / 100);
   ui_grp->r_item[r_cnt].w = (ui_grp->r_item[r_cnt].w * fb->w / 100);
   ui_grp->r_item[r_cnt].h = (ui_grp->r_item[r_cnt].h * fb->h / 100);

   if ((signed)ui_grp->r_item[r_cnt].bc.uint < 0)
      ui_grp->r_item[r_cnt].bc.uint = ui_grp->bc.uint;

   if ((signed)ui_grp->r_item[r_cnt].lc.uint < 0)
      ui_grp->r_item[r_cnt].lc.uint = ui_grp->lc.uint;

   r_cnt++;
   ui_grp->r_cnt = r_cnt;
}

//------------------------------------------------------------------------------
static void _ui_parser_cmd_S (char *buf, fb_info_t *fb, ui_grp_t *ui_grp)
{
   int s_cnt = ui_grp->s_cnt;
   char *ptr = strtok (buf, ",");

   ptr = strtok (NULL, ",");     ui_grp->s_item[s_cnt].r_id    = atoi(ptr);
   ptr = strtok (NULL, ",");     ui_grp->s_item[s_cnt].x       = atoi(ptr);
   ptr = strtok (NULL, ",");     ui_grp->s_item[s_cnt].y       = atoi(ptr);
   ptr = strtok (NULL, ",");     ui_grp->s_item[s_cnt].scale   = atoi(ptr);

   ptr = strtok (NULL, ",");
   ui_grp->s_item[s_cnt].fc.uint = strtoul(ptr, NULL, 16);
   ptr = strtok (NULL, ",");
   ui_grp->s_item[s_cnt].bc.uint = strtoul(ptr, NULL, 16);

   if ((signed)ui_grp->s_item[s_cnt].fc.uint < 0)
      ui_grp->s_item[s_cnt].fc.uint = ui_grp->fc.uint;

   /* 문자열이 없거나 앞부분의 공백이 있는 경우 제거 */
   if ((ptr = strtok (NULL, ",")) != NULL) {
      int slen = strlen(ptr);

      while ((*ptr == 0x20) && slen--)
         ptr++;
      strncpy(ui_grp->s_item[s_cnt].str, ptr, slen);
   }
   ptr = strtok (NULL, ",");     ui_grp->s_item[s_cnt].f_type = atoi(ptr);

   if (ui_grp->s_item[s_cnt].r_id >= ITEM_COUNT_MAX) {
      if (ui_grp->s_item[s_cnt].x < 0)          ui_grp->s_item[s_cnt].x = 0;
      if (ui_grp->s_item[s_cnt].y < 0)          ui_grp->s_item[s_cnt].y = 0;
      if (ui_grp->s_item[s_cnt].scale   < 0)    ui_grp->s_item[s_cnt].scale = 1;

      if (ui_grp->s_item[s_cnt].f_type  < 0)
         ui_grp->s_item[s_cnt].f_type  = ui_grp->f_type;
      if (ui_grp->s_item[s_cnt].fc.uint < 0)
         ui_grp->s_item[s_cnt].fc.uint = ui_grp->fc.uint;
      if (ui_grp->s_item[s_cnt].bc.uint < 0)
         ui_grp->s_item[s_cnt].bc.uint = ui_grp->bc.uint;
   }
   s_cnt++;
   ui_grp->s_cnt = s_cnt;
}

//------------------------------------------------------------------------------
static void _ui_parser_cmd_G (char *buf, fb_info_t *fb, ui_grp_t *ui_grp)
{
   int pos = ui_grp->r_cnt;
   int s_h, r_h, sid, r_cnt, g_cnt, bc, lw, lc, i, j, y_s;
   char *ptr = strtok (buf, ",");

   ptr = strtok (NULL, ",");     sid   = atoi(ptr);
   ptr = strtok (NULL, ",");     r_cnt = atoi(ptr);
   ptr = strtok (NULL, ",");     s_h   = atoi(ptr);
   ptr = strtok (NULL, ",");     r_h   = atoi(ptr);
   ptr = strtok (NULL, ",");     g_cnt = atoi(ptr);
   ptr = strtok (NULL, ",");     bc    = strtol(ptr, NULL, 16);
   ptr = strtok (NULL, ",");     lw    = atoi(ptr);
   ptr = strtok (NULL, ",");     lc    = strtol(ptr, NULL, 16);

   for (i = 0; i < g_cnt; i++) {
      for (j = 0; j < r_cnt; j++) {
         pos = ui_grp->r_cnt + j + i * r_cnt;

         ui_grp->r_item[pos].w = (fb->w / r_cnt);
         y_s                   = (fb->h * s_h) / 100;
         ui_grp->r_item[pos].h = (fb->h * r_h) / 100;

         ui_grp->r_item[pos].id = sid + j + i * r_cnt;
         ui_grp->r_item[pos].x  = ui_grp->r_item[pos].w * j;
         ui_grp->r_item[pos].y  = ui_grp->r_item[pos].h * i + y_s;
         ui_grp->r_item[pos].lw = lw;

         ui_grp->r_item[pos].bc.uint = bc < 0 ? ui_grp->bc.uint : bc;
         ui_grp->r_item[pos].lc.uint = lc < 0 ? ui_grp->lc.uint : lc;
      }
   }
   ui_grp->r_cnt = pos +1;
}

//------------------------------------------------------------------------------
void ui_set_ritem (fb_info_t *fb, ui_grp_t *ui_grp,
                     int f_id, int bc, int lc)
{
   int s_rid = 0;
   r_item_t *r_item;

   if (f_id < ITEM_COUNT_MAX) {
      /* 같은 아이디를 찾아 모두 바꾼다. */
      while ((r_item = _ui_find_r_item(ui_grp, &s_rid, f_id)) != NULL) {
         if (bc != -1)  r_item->bc.uint = bc;   
         if (lc != -1)  r_item->lc.uint = lc;
         ui_set_sitem (fb, ui_grp, f_id, -1, bc, NULL);
         ui_update (fb, ui_grp, f_id);
      }
   }
}

//------------------------------------------------------------------------------
void ui_set_sitem (fb_info_t *fb, ui_grp_t *ui_grp,
                     int id, int fc, int bc, char *str)
{
   int n_sid = 0, n_rid = 0;
   s_item_t *s_item;
   r_item_t *r_item;

   if (id < ITEM_COUNT_MAX) {
      while ((r_item = _ui_find_r_item(ui_grp, &n_rid, id)) != NULL) {
         n_sid = 0;
         while ((s_item = _ui_find_s_item(ui_grp, &n_sid, id)) != NULL) {

            /* font color 변경 */
            if (fc != -1)
               s_item->fc.uint = fc;
            if (bc != -1)
               s_item->bc.uint = bc;

            /* 받아온 string을 buf에 저장 */
            if (str != NULL)  {
               char buf[ITEM_STR_MAX];

               memset(buf, 0x00, sizeof(buf));

               sprintf(buf, "%s", str);
               /*
                  기존 문자열 보다 새로운 문자열이 더 작은 경우
                  기존 문자열을 배경색으로 덮어 씌운다.
               */
               if ((strlen(s_item->str) > strlen(buf)))
                  _ui_clr_str (fb, r_item, s_item);

               /* 새로운 string 복사 */
               strncpy(s_item->str, buf, strlen(buf));
            }

            _ui_str_pos_xy(r_item, s_item);
            _ui_update_s (fb, s_item, r_item->x, r_item->y);
         }
      }
   }
}

//------------------------------------------------------------------------------
void ui_set_str (fb_info_t *fb, ui_grp_t *ui_grp,
                  int id, int x, int y, int scale, int font, char *fmt, ...)
{
   int n_sid = 0, n_rid = 0;
   s_item_t *s_item;
   r_item_t *r_item;

   if (id < ITEM_COUNT_MAX) {
      while ((r_item = _ui_find_r_item(ui_grp, &n_rid, id)) != NULL) {
         n_sid = 0;
         while ((s_item = _ui_find_s_item(ui_grp, &n_sid, id)) != NULL) {
            va_list va;
            char buf[ITEM_STR_MAX];
            int n_scale = s_item->scale;

            /* 받아온 가변인자를 string 형태로 변환 하여 buf에 저장 */
            memset(buf, 0x00, sizeof(buf));
            va_start(va, fmt);   vsprintf(buf, fmt, va); va_end(va);

            if (scale) {
               /* scale = -1 이면 최대 스케일을 구하여 표시한다 */
               if (scale < 0)
                  n_scale = _ui_str_scale (r_item->w, r_item->h,
                                             r_item->lw, _my_strlen(buf));
               else
                  n_scale = scale;

               if (s_item->scale > n_scale)
                  _ui_clr_str (fb, r_item, s_item);
            }

            if (font) {
               s_item->f_type = (font < 0) ? ui_grp->f_type : font;
               set_font(s_item->f_type);
            }

            /*
               기존 문자열 보다 새로운 문자열이 더 작은 경우
               기존 문자열을 배경색으로 덮어 씌운다.
            */
            if ((strlen(s_item->str) > strlen(buf)) || n_scale != s_item->scale) {
               _ui_clr_str (fb, r_item, s_item);
               s_item->scale = n_scale;
            }
            s_item->x = (x != 0) ? x : s_item->x;
            s_item->y = (y != 0) ? x : s_item->y;

            /* 새로운 string 복사 */
            strncpy(s_item->str, buf, strlen(buf));

            _ui_str_pos_xy(r_item, s_item);
            _ui_update_s (fb, s_item, r_item->x, r_item->y);
         }
      }
   } else {
      int i;
      for (i = 0; i < ui_grp->s_cnt; i++) {
         if (ui_grp->s_item[i].r_id == id) {
            int color = ui_grp->s_item[i].fc.uint;

            /* 기존 문자열을 벼경색으로 다시 그려서 지움 */
            ui_grp->s_item[i].fc.uint = ui_grp->s_item[i].bc.uint;
            _ui_update_s (fb, &ui_grp->s_item[i], 0, 0);
            ui_grp->s_item[i].fc.uint = color;
            ui_grp->s_item[i].scale = (scale > 0) ? scale : 1;
            ui_grp->s_item[i].f_type = font;
            ui_grp->s_item[i].x = x;
            ui_grp->s_item[i].y = y;
            _ui_update_s (fb, &ui_grp->s_item[i], 0, 0);
         }
      }
   }
}

//------------------------------------------------------------------------------
void ui_set_printf (fb_info_t *fb, ui_grp_t *ui_grp, int id, char *fmt, ...)
{
   va_list va;
   char buf[ITEM_STR_MAX];

   /* 받아온 가변인자를 string 형태로 변환 하여 buf에 저장 */
   memset(buf, 0x00, sizeof(buf));
   va_start(va, fmt);   vsprintf(buf, fmt, va); va_end(va);

   ui_set_str (fb, ui_grp, id, -1, -1, -1, -1, buf);
}

//------------------------------------------------------------------------------
void ui_update (fb_info_t *fb, ui_grp_t *ui_grp, int id)
{
   int i, sid, fid;
   r_item_t *r_item;

   /* ui_grp에 등록되어있는 모든 item에 대하여 화면 업데이트 함 */
   if (id < 0) {
      /* 사각형 item에 대한 화면 업데이트 */
      for (i = 0; i < ui_grp->r_cnt; i++)
         _ui_update (fb, ui_grp, i);

      /* 문자열 item에 대한 화면 업데이트 */
      for (i = 0; i < ui_grp->s_cnt; i++) {
         if (ui_grp->s_item[i].r_id >= ITEM_COUNT_MAX)
            _ui_update_s (fb, &ui_grp->s_item[i], 0, 0);
      }
   }
   else  /* id값으로 설정된 1 개의 item에 대한 화면 업데이트 */
      _ui_update (fb, ui_grp, id);

}

//------------------------------------------------------------------------------
void ui_close (ui_grp_t *ui_grp)
{
   /* 할당받은 메모리가 있다면 시스템으로 반환한다. */
   if (ui_grp)
      free (ui_grp);
}

//------------------------------------------------------------------------------
ui_grp_t *ui_init (fb_info_t *fb, const char *cfg_filename)
{
   ui_grp_t	*ui_grp;
   FILE *pfd;
   char buf[256], r_cnt = 0, s_cnt = 0, *ptr, is_cfg_file = 0;

   if ((pfd = fopen(cfg_filename, "r")) == NULL)
      return   NULL;

	if ((ui_grp = (ui_grp_t *)malloc(sizeof(ui_grp_t))) == NULL)
      return   NULL;

   memset (ui_grp, 0x00, sizeof(ui_grp_t));
   memset (buf,    0x00, sizeof(buf));

   while(fgets(buf, sizeof(buf), pfd) != NULL) {
      if (!is_cfg_file) {
         is_cfg_file = strncmp ("ODROID-UI-CONFIG", buf, strlen(buf)-1) == 0 ? 1 : 0;
         memset (buf, 0x00, sizeof(buf));
         continue;
      }
      switch(buf[0]) {
         case  'C':  _ui_parser_cmd_C (buf, fb, ui_grp); break;
         case  'R':  _ui_parser_cmd_R (buf, fb, ui_grp); break;
         case  'S':  _ui_parser_cmd_S (buf, fb, ui_grp); break;
         case  'G':  _ui_parser_cmd_G (buf, fb, ui_grp); break;
         default :
            err("Unknown parser command! cmd = %c\n", buf[0]);
         case  '#':  case  '\n':
         break;
      }
      memset (buf, 0x00, sizeof(buf));
   }

   if (!is_cfg_file) {
      err("UI Config File not found! (filename = %s)\n", cfg_filename);
      free (ui_grp);
      return NULL;
   }

   /* all item update */
   if (ui_grp->r_cnt)
      ui_update (fb, ui_grp, -1);

   if (pfd)
      fclose (pfd);

	// file parser
	return	ui_grp;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
