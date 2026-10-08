<?php
$message = "";

if ($_SERVER["REQUEST_METHOD"] === "POST")
{
    $bin = $_POST["bin"] ?? "";

    if (in_array($bin, ["A", "B", "C"]))
    {
        $conn = new mysqli("127.0.0.1", "iot", "CHANGE_ME", "iotdb");

        if ($conn->connect_error)
            die("DB 연결 실패");

        $stmt = $conn->prepare(
            "INSERT INTO request_history (bin_name) VALUES (?)"
        );

        if ($stmt === false)
            die("DB prepare 실패");

        $stmt->bind_param("s", $bin);

        if ($stmt->execute())
        {
            $socket = fsockopen("127.0.0.1", 5000, $errno, $errstr, 2);

            if ($socket)
            {
                fwrite($socket, "REQUEST@" . $bin . "\n");
                fclose($socket);
            }

            $message = $bin . " 쓰레기통 수거 요청이 접수되었습니다.";
        }
        else
        {
            $message = "수거 요청 저장에 실패했습니다.";
        }

        $stmt->close();
        $conn->close();
    }
}
?>
<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>음식물 쓰레기 수거 요청</title>
<style>
* { box-sizing: border-box; }
body { margin:0; padding:20px; font-family:Arial,sans-serif; background:#f3f5f7; }
.container { max-width:500px; margin:0 auto; }
h1 { text-align:center; margin-bottom:10px; }
.description { text-align:center; color:#666; margin-bottom:30px; line-height:1.6; }
.bin { background:white; padding:20px; margin-bottom:18px; border-radius:15px; box-shadow:0 3px 10px rgba(0,0,0,.10); }
.bin h2 { margin-top:0; }
button { width:100%; padding:15px; border:none; border-radius:10px; font-size:18px; font-weight:bold; cursor:pointer; background:#ff6b6b; color:white; }
button:active { transform:scale(.98); }
.message { background:#dff5df; padding:15px; margin-bottom:20px; border-radius:10px; text-align:center; font-weight:bold; }
</style>
</head>
<body>
<div class="container">
<h1>음식물 쓰레기 수거 요청</h1>
<div class="description">음식물 쓰레기통이 가득 찼다면<br>해당 위치의 수거 요청 버튼을 눌러주세요.</div>
<?php if ($message !== "") { ?>
<div class="message"><?php echo htmlspecialchars($message); ?></div>
<?php } ?>
<?php foreach (["A","B","C"] as $b) { ?>
<div class="bin">
  <h2><?php echo $b; ?> 쓰레기통</h2>
  <form method="post">
    <button type="submit" name="bin" value="<?php echo $b; ?>"><?php echo $b; ?> 수거 요청</button>
  </form>
</div>
<?php } ?>
</div>
</body>
</html>
