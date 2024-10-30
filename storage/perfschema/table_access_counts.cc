/* Copyright (c) 2017, 2022, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file storage/perfschema/table_access_counts.cc
  Table access_counts (implementation).
*/

#include "storage/perfschema/table_access_counts.h"

#include "mysql/plugin.h"
#include "sql/current_thd.h"
#include "sql/debug_sync.h"
#include "sql/field.h"
#include "sql/mysqld.h"
#include "sql/plugin_table.h"
#include "storage/perfschema/pfs_instr.h"
#include "storage/perfschema/pfs_instr_class.h"
#include "storage/perfschema/table_helper.h"


THR_LOCK table_access_counts::m_table_lock;

Plugin_table table_access_counts::m_table_def(
    /* Schema name */
    "performance_schema",
    /* Name */
    "access_counts",
    /* Definition */
    "  TABLE_NAME CHAR(255) not null,\n"
    "  ACCESS_COUNTS BIGINT unsigned not null\n",
    /* Options */
    " ENGINE=PERFORMANCE_SCHEMA",
    /* Tablespace */
    nullptr);

PFS_engine_table_share table_access_counts::m_share = {
    &pfs_truncatable_acl,
    table_access_counts::create,
    nullptr,                                   /* write_row */
    table_access_counts::delete_all_rows,
    table_access_counts::get_row_count, /* records */
    sizeof(PFS_simple_index),                  /* ref length */
    &m_table_lock,
    &m_table_def,
    true, /* perpetual */
    PFS_engine_table_proxy(),
    {0},
    false /* m_in_purgatory */
};

PFS_engine_table *table_access_counts::create(PFS_engine_table_share *) {
  return new table_access_counts();
}

table_access_counts::table_access_counts()
    : PFS_engine_table(&m_share, &m_pos), m_pos(0), m_next_pos(0) {}


int table_access_counts::delete_all_rows(void) {
    table_access_count.clear();
    return 0;
}

table_access_counts::~table_access_counts() = default;

void table_access_counts::reset_position(void) {
  m_pos.m_index = 0;
  m_next_pos.m_index = 0;
}

ha_rows table_access_counts::get_row_count() { return table_access_count.size(); }

int table_access_counts::rnd_next(void) {
  int res = HA_ERR_END_OF_FILE;
  uint cnt = table_access_count.size();

  for (m_pos.set_at(&m_next_pos); m_pos.m_index < cnt; m_pos.next()) {
    m_next_pos.set_after(&m_pos);
    return 0;
  }

  return res;
}

int table_access_counts::rnd_pos(const void *pos) {
  int res = HA_ERR_RECORD_DELETED;
  return res;
}

int table_access_counts::read_row_values(
    TABLE *table, unsigned char *buf,
    Field **fields, bool read_all ) {
  Field *f;

  /* Set the null bits */
  assert(table->s->null_bytes == 1);
  buf[0] = 0;
  buf[1] = 0;

  /* prepare row */
  auto it = table_access_count.begin();
  for (uint cnt = 0; it != table_access_count.end() && cnt < m_pos.m_index; it++) {
    cnt++;
  }
  std::string tbl = it->first;
  uint tlen = tbl.length();
  char tname[255];
  std::strcpy(tname, tbl.c_str());

  /* read row */
  for (; (f = *fields); fields++) {
    if (read_all || bitmap_is_set(table->read_set, f->field_index())) {
      switch (f->field_index()) {
        case 0: /* table_name */
          set_field_char_utf8mb4(f, tname, tlen);
          break;
        case 1: /* access_count */
          set_field_ulonglong(f, it->second);
          break;
        default:
          assert(false);
      }
    }
  }
  return 0;
}