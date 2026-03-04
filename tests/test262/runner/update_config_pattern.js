#!/usr/bin/env node
/**
 * Set the single pattern in test262_paths.json.
 * Usage: node update_config_pattern.js <pattern>
 */
const fs = require("fs");
const path = require("path");
const pattern = process.argv[2];
if (!pattern) process.exit(1);
const repoRoot = path.resolve(__dirname, "../../..");
const configPath = path.join(repoRoot, "tests", "test262", "config", "test262_paths.json");
const cfg = JSON.parse(fs.readFileSync(configPath, "utf8"));
cfg.patterns = [pattern];
fs.writeFileSync(configPath, JSON.stringify(cfg, null, 2));
