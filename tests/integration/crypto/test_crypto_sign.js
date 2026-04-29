console.log("=== crypto createSign / createVerify ===");
let pass = 0, fail = 0;

function check(label, cond, detail) {
    if (cond) { console.log("PASS", label); pass++; }
    else { console.log("FAIL", label, detail || ""); fail++; }
}

const privPem = `-----BEGIN PRIVATE KEY-----
MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQC9g8xIv0vrzU+j
xh3qZo70WAiDzmtb3hVTKPBncVpL3A1Ak/KM9GNd3UlQHU2dHiMibViq0BjFbAYk
sFypXYIH+S7+6DY3yKFtI6eVoaZKuZ+St6uissJHFRxwYNNVvZU4V9uwsp81aQ5h
g6daoWyWD/bZJ2zwK3CQyb5pIKJUPPP7XOrTBkAFl8HfWbLVdThJT8yVX0LoxoNE
61UbXxVNpHWhkdWdwDpez5i/MphpoS2YqkcnP9nh9tJggcnUTbjlArdFzQ923JiQ
FcGuGWOn0mrTG8KxmQ+/hcRbRRp39XXFTh3Lr6dcH3cROaaAaVL1AFj13B4Z5gr6
1M66xd2LAgMBAAECggEANgEeUCavKFlf1fApazfhidCiUIkcf9fX5NQ4OYoMLKze
2+WtCyzDOibKcvl+ugSyB2f4ieh1/qArWf4l0Z+TY9lG4p3igV/7XEH6SgIABLBD
wEf1sY3WHfJuobl9z6OQJ7elo4MrKkmvLuuY35M7gcG32qbI/OGsGJ5c09FYYL9m
IUnIdZaqwWIXJKqVQzdVNSJGrkZVhvgq/xoCpg7cuwz0l2StiTz9NcBNOIvroZtu
Rnzp2MuPoHdPJvrTpeNuAkKxcyJ/Do4RAiyT0LpBagA+SagSrJ6xbJ28J7RTx1AG
+GV71taxZ8s3YQowZ7QHG3Yw0iO/IXno5k9CSoERdQKBgQDohuXZL9y/Wd5TChr4
C/DTeisOW8JM4XWw4DZKDYr0pOlZmahZgJsotd1+gNJkl+gf+QcTgvqmU98bcj/U
U84BnNp9GdGmyM4fsy45TsUpEnOeItT9l9k7vHq1DJU2vFrcKOaptmaXQZCvSyh7
KDpzdh4TyXpnTHGe/xK1B9PhVQKBgQDQpVrsY18DXfuQ2+yrO0iivA4+h4OsQYJu
S5uuKYSqlpKGjmjGwsTZCV3AD+eHvO1iZMtQIWc5p5cLcTHgxBWJZ/ab5aXpJhEO
H7OLyAx8tNWbovfUOznAZ86azHvZIL/jYyr27TCMzcPVgyNHL705aX7gwJprZlZZ
PequUnxDXwKBgD2+JCYpeVouCMTP+B1JPmdJF0m2v78eVtvijUfYlL8lUvkBvhwV
9B05PVkr57HiTDbBL0nVC61CtAlbqus8XYU8GyAAzRSWWXU9ZNa+vceMKLsi0J+N
xJcCEysj7jMcjJvNGIKT9mXPeRWyxUr+gZbLFG14oFHxkHIBlPwQ2ggJAoGASXzk
QHxjm8D/eS8s9cakt8S606VRaFuOgCCbTcWL17W/GCuSledGBBe7cIlpiDKv/cb4
oVmSjQkNN1eANOV7nHEEuDYzsKHawfnCeIpWc5oR3oaQ+ax+k9k8OOOq/3f8fi+Q
k3ZJcl6LCmntBAa5hD43FRxhh1B9O2OGhC3DXMsCgYAOKwPHvtMhLSmqZHJF8Jzz
N02b6DzBbFWl9sRXbkr7NW7IKVeRYrn5MINvaXtSc8tsTJ5Cia3VR8G6DuB1geNd
fP/e0zcrTj+wjSHGWhPVWj+ggoxLYp6EappzqwOMYMGGiVWWo2z1Wl6OD9N/0Q+S
m4fvLC+B4CpTyg/jMjtjMg==
-----END PRIVATE KEY-----`;

const pubPem = `-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvYPMSL9L681Po8Yd6maO
9FgIg85rW94VUyjwZ3FaS9wNQJPyjPRjXd1JUB1NnR4jIm1YqtAYxWwGJLBcqV2C
B/ku/ug2N8ihbSOnlaGmSrmfkrerorLCRxUccGDTVb2VOFfbsLKfNWkOYYOnWqFs
lg/22Sds8CtwkMm+aSCiVDzz+1zq0wZABZfB31my1XU4SU/MlV9C6MaDROtVG18V
TaR1oZHVncA6Xs+YvzKYaaEtmKpHJz/Z4fbSYIHJ1E245QK3Rc0PdtyYkBXBrhlj
p9Jq0xvCsZkPv4XEW0Uad/V1xU4dy6+nXB93ETmmgGlS9QBY9dweGeYK+tTOusXd
iwIDAQAB
-----END PUBLIC KEY-----`;

const data = "message to sign";

// Sign
const signer = crypto.createSign('RSA-SHA256');
check("createSign returns object", typeof signer === 'object');
signer.update(data);
const sig = signer.sign(privPem);
check("sign() returns hex string", typeof sig === 'string' && sig.length > 0, `len=${sig.length}`);

// Verify
const verifier = crypto.createVerify('RSA-SHA256');
check("createVerify returns object", typeof verifier === 'object');
verifier.update(data);
const ok = verifier.verify(pubPem, sig);
check("verify() returns true for valid signature", ok === true, `got ${ok}`);

// Verify failure with wrong data
const verifier2 = crypto.createVerify('RSA-SHA256');
verifier2.update("wrong data");
const bad = verifier2.verify(pubPem, sig);
check("verify() returns false for wrong data", bad === false, `got ${bad}`);

console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) process.exit(1);
