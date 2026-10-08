<?php
$conn = new mysqli("127.0.0.1", "iot", "CHANGE_ME", "iotdb");
if ($conn->connect_error) die("DB CONNECT ERROR : " . $conn->connect_error);
$conn->set_charset("utf8mb4");

$status_result = $conn->query(
    "SELECT bin_a, bin_b, bin_c, created_at FROM food_bin ORDER BY id DESC LIMIT 1"
);

$bin_a = -1; $bin_b = -1; $bin_c = -1; $updated_at = "-";
if ($status_result && $status_result->num_rows > 0) {
    $row = $status_result->fetch_assoc();
    $bin_a = (int)$row["bin_a"];
    $bin_b = (int)$row["bin_b"];
    $bin_c = (int)$row["bin_c"];
    $updated_at = $row["created_at"];
}

$history_result = $conn->query(
    "SELECT bin_name, collected_at FROM collection_history ORDER BY id DESC LIMIT 10"
);

$count_result = $conn->query(
    "SELECT COUNT(*) AS total FROM collection_history WHERE DATE(collected_at) = CURDATE()"
);
$today_count = 0;
if ($count_result && $count_result->num_rows > 0)
    $today_count = (int)$count_result->fetch_assoc()["total"];

// 수거 경로: /home/pi/food_project/webcam/route_view.py 가 route/ 폴더에 route.jpg, status.json 을 2초마다 저장
$route_img = __DIR__ . "/route/route.jpg";
$route = null;
if (is_file(__DIR__ . "/route/status.json"))
    $route = json_decode(file_get_contents(__DIR__ . "/route/status.json"), true);

function getLevelClass($percent) {
    if ($percent < 0) return "sensor-error";
    if ($percent <= 40) return "green";
    if ($percent <= 75) return "yellow";
    if ($percent <= 90) return "red";
    return "danger";
}
function getStatusText($percent) {
    if ($percent < 0) return "센서 오류";
    if ($percent <= 40) return "여유";
    if ($percent <= 75) return "주의";
    if ($percent <= 90) return "혼잡";
    return "수거 필요";
}
function getPercentText($percent) {
    return $percent < 0 ? "--" : $percent . "%";
}
?>
<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta http-equiv="refresh" content="3">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Food Waste Monitoring</title>
<style>
* { box-sizing:border-box; }
body { margin:0; padding:25px; font-family:Arial,"Noto Sans KR",sans-serif; background:#f3f5f7; color:#222; }
.container { width:100%; max-width:900px; margin:0 auto; }
h1 { text-align:center; margin-bottom:30px; font-size:34px; }
.summary { display:flex; justify-content:space-between; align-items:center; gap:15px; margin-bottom:20px; padding:15px 20px; background:white; border-radius:15px; box-shadow:0 2px 8px rgba(0,0,0,.08); }
.summary-item { font-size:16px; }
.summary-value { font-size:22px; font-weight:bold; }
.status-box { display:grid; grid-template-columns:repeat(3,1fr); gap:18px; }
.bin { position:relative; min-height:180px; padding:25px 15px; border-radius:18px; text-align:center; box-shadow:0 5px 12px rgba(0,0,0,.15); transition:.2s; }
.bin-name { font-size:21px; font-weight:bold; }
.percent { margin-top:16px; font-size:46px; font-weight:bold; }
.state { margin-top:10px; font-size:17px; font-weight:bold; }
.green { background:#78d94f; }
.yellow { background:#ffd86a; }
.red { background:#ff7474; color:white; }
.danger { background:#e53935; color:white; animation:warningBlink .7s infinite alternate; }
.sensor-error { background:#777; color:white; }
@keyframes warningBlink { from{opacity:1;transform:scale(1)} to{opacity:.55;transform:scale(1.02)} }
.update-time { margin-top:22px; text-align:center; font-size:16px; color:#666; }
.history { margin-top:35px; padding:25px; background:white; border-radius:18px; box-shadow:0 2px 8px rgba(0,0,0,.08); }
.history h2 { margin-top:0; margin-bottom:20px; font-size:25px; }
.history-item { display:flex; justify-content:space-between; align-items:center; gap:15px; padding:13px 5px; border-bottom:1px solid #ddd; }
.history-item:last-child { border-bottom:none; }
.history-bin { font-weight:bold; }
.route-img { display:block; width:100%; border-radius:12px; }
.route-text { margin-top:14px; font-size:18px; font-weight:bold; }
.route-warn { margin-top:6px; color:#e53935; }
@media (max-width:650px) {
 body{padding:15px} h1{font-size:27px;margin-bottom:22px} .status-box{grid-template-columns:1fr}
 .bin{min-height:150px} .percent{font-size:43px} .summary{flex-direction:column;align-items:flex-start}
 .history-item{flex-direction:column;align-items:flex-start}
}
</style>
</head>
<body>
<div class="container">
<h1>음식물 쓰레기 관제 시스템</h1>
<div class="summary">
  <div class="summary-item">오늘 수거 횟수<div class="summary-value"><?php echo $today_count; ?>회</div></div>
  <div class="summary-item">시스템 상태<div class="summary-value">실시간 관제 중</div></div>
</div>
<div class="status-box">
<?php foreach (["A"=>$bin_a,"B"=>$bin_b,"C"=>$bin_c] as $name=>$value) { ?>
  <div class="bin <?php echo getLevelClass($value); ?>">
    <div class="bin-name"><?php echo $name; ?> 쓰레기통</div>
    <div class="percent"><?php echo getPercentText($value); ?></div>
    <div class="state"><?php echo getStatusText($value); ?></div>
  </div>
<?php } ?>
</div>
<div class="update-time">마지막 업데이트 : <?php echo htmlspecialchars($updated_at); ?></div>
<div class="history">
<h2>수거 경로</h2>
<?php if (is_file($route_img)) { ?>
  <img class="route-img" src="route/route.jpg?t=<?php echo filemtime($route_img); ?>" alt="수거 경로">
<?php } ?>
<div class="route-text">
<?php if (!$route) { ?>
  경로 화면이 아직 없습니다. (route_view.py 실행 필요)
<?php } else { ?>
  <?php echo empty($route["order"]) ? "수거 대상 없음" : "수거 순서 : " . htmlspecialchars(implode(" → ", $route["order"])); ?>
  <?php if (time() - (int)$route["unix"] > 30) { ?>
  <div class="route-warn">경로 화면 갱신 중지됨</div>
  <?php } else { ?>
  <?php if (empty($route["db_ok"])) { ?><div class="route-warn">DB 연결 오류</div><?php } ?>
  <?php if (empty($route["camera_ok"])) { ?><div class="route-warn">카메라 연결 끊김</div><?php } ?>
  <?php } ?>
<?php } ?>
</div>
</div>
<div class="history">
<h2>최근 수거 기록</h2>
<?php if ($history_result && $history_result->num_rows > 0) { while ($history = $history_result->fetch_assoc()) { ?>
  <div class="history-item">
    <div class="history-bin">쓰레기통 <?php echo htmlspecialchars($history["bin_name"]); ?></div>
    <div><?php echo htmlspecialchars($history["collected_at"]); ?></div>
  </div>
<?php } } else { ?>
  <div class="history-item">아직 수거 기록이 없습니다.</div>
<?php } $conn->close(); ?>
</div>
</div>
</body>
</html>
