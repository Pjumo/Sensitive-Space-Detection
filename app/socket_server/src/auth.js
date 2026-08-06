'use strict';

const fs = require('fs');

/**
 * app/socket_server/idpasswd.txt 와 동일한 포맷("id passwd" 줄바꿈 구분)을
 * 읽어서 Map<id, passwd> 로 반환합니다.
 */
function loadUsers(filePath) {
  const text = fs.readFileSync(filePath, 'utf8');
  const users = new Map();

  for (const line of text.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) continue;

    const parts = trimmed.split(/\s+/);
    if (parts.length !== 2) continue;

    const [id, passwd] = parts;
    users.set(id, passwd);
  }

  return users;
}

function checkAuth(users, id, passwd) {
  return users.has(id) && users.get(id) === passwd;
}

module.exports = { loadUsers, checkAuth };
