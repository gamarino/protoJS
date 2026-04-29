console.log("=== crypto.generateKeyPair ===");
let pass = 0, fail = 0;

function check(label, cond, detail) {
    if (cond) { console.log("PASS", label); pass++; }
    else { console.log("FAIL", label, detail || ""); fail++; }
}

const kp = crypto.generateKeyPair('rsa', { modulusLength: 2048 });
check("returns object",    typeof kp === 'object');
check("publicKey is string",  typeof kp.publicKey === 'string');
check("privateKey is string", typeof kp.privateKey === 'string');
check("publicKey is PEM",  kp.publicKey.includes('BEGIN PUBLIC KEY') ||
                           kp.publicKey.includes('BEGIN RSA PUBLIC KEY'));
check("privateKey is PEM", kp.privateKey.includes('BEGIN RSA PRIVATE KEY') ||
                           kp.privateKey.includes('BEGIN PRIVATE KEY'));
check("keys are not placeholder", kp.publicKey !== 'placeholder');

// Use the generated keys with createSign / createVerify
const signer = crypto.createSign('RSA-SHA256');
signer.update('roundtrip');
const sig = signer.sign(kp.privateKey);
check("sign with generated key returns hex", typeof sig === 'string' && sig.length > 0);

const verifier = crypto.createVerify('RSA-SHA256');
verifier.update('roundtrip');
const ok = verifier.verify(kp.publicKey, sig);
check("verify with generated public key", ok === true, `got ${ok}`);

console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) process.exit(1);
