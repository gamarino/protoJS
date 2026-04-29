console.log("=== crypto Cipher/Decipher ===");
let pass = 0, fail = 0;

function check(label, cond, detail) {
    if (cond) { console.log("PASS", label); pass++; }
    else { console.log("FAIL", label, detail || ""); fail++; }
}

// createCipheriv / createDecipheriv roundtrip (AES-256-CBC, explicit key+IV)
const key = '12345678901234567890123456789012';   // 32 bytes
const iv  = '1234567890123456';                    // 16 bytes
const plaintext = 'Hello, protoJS!';

const cipher = crypto.createCipheriv('aes-256-cbc', key, iv);
let encrypted = cipher.update(plaintext);
encrypted += cipher.final();
check("createCipheriv returns object", typeof cipher === 'object');
check("update() returns string", typeof encrypted === 'string');
check("encrypted differs from plaintext", encrypted !== plaintext);

const decipher = crypto.createDecipheriv('aes-256-cbc', key, iv);
let decrypted = decipher.update(encrypted);
decrypted += decipher.final();
check("createDecipheriv roundtrip", decrypted === plaintext, `got: ${decrypted}`);

// createCipher / createDecipher (key-derivation variants)
const c2 = crypto.createCipher('aes-128-cbc', 'secret');
let enc2 = c2.update('test data');
enc2 += c2.final();
check("createCipher returns object", typeof c2 === 'object');
check("createCipher encrypts", enc2 !== 'test data');

const d2 = crypto.createDecipher('aes-128-cbc', 'secret');
let dec2 = d2.update(enc2);
dec2 += d2.final();
check("createDecipher roundtrip", dec2 === 'test data', `got: ${dec2}`);

console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) process.exit(1);
