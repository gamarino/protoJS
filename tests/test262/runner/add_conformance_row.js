#!/usr/bin/env node
/**
 * Insert one conformance table row into CONFORMANCE_JS.md before the "built-ins/Object mini-suite" section.
 * Usage: node add_conformance_row.js <pattern> <total> <passed> <failed_syntax> <failed_semantics> <timeout>
 */
const fs = require("fs");
const path = require("path");
const args = process.argv.slice(2);
if (args.length < 6) process.exit(1);
const [pattern, total, passed, fs_, fm, to] = args;
const repoRoot = path.resolve(__dirname, "../../..");
const docPath = path.join(repoRoot, "CONFORMANCE_JS.md");
const marker = "The `built-ins/Object` mini-suite provides";
const row = `| \`${pattern}\` | ${total} | ${passed} | ${fs_} | ${fm} | ${to} | Official Test262 subset. |`;
let content = fs.readFileSync(docPath, "utf8");
const idx = content.indexOf(marker);
if (idx === -1) throw new Error("Marker not found");
const insertAt = content.lastIndexOf("\n", idx);
content = content.slice(0, insertAt + 1) + row + "\n" + content.slice(insertAt + 1);
fs.writeFileSync(docPath, content);
