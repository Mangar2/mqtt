<?php

$topic ='';
$history = 'false';
$reason = 'true';
$time = 'true';
$levelAmount = 1;
$nodes = '';

$postdata = file_get_contents("php://input");

if (!empty($postdata)) {
    $request = json_decode($postdata);
    if (!empty($request)) {
        if (property_exists($request, "topic")) {
            $topic = filter_var($request->topic);
        }
        if (property_exists($request, "history")) {
            $history =  $request->history == 'true' ? 'true' : 'false';
        }
        if (property_exists($request, "reason")) {
            $reason =  $request->reason == 'true' ? 'true' : 'false';
        }
        if (property_exists($request, "time")) {
            $time =  $request->time == 'true' ? 'true' : 'false';
        }
        if (property_exists($request, "levelAmount")) {
            $levelAmount = $request->levelAmount;
            /*
            $levelAmount = filter_var($request->levelAmount, FILTER_VALIDATE_INT, [
                'options' => [
                    'default' => 1,
                    'min-range' => 1,
                    'max-range' => 10
                ]
            ]);
            */
        }
        if (property_exists($request, "nodes")) {
            $nodes = json_encode($request->nodes);
        }
    }
}

if (!empty($topic) && $topic[0] != '/') {
    $topic = '/' . $topic;
}
$topic = str_replace(' ', '%20', $topic);

// Create a stream
$opts = [
    "http" => [
        "method" => "GET",
        "header" => 
            "history: " . $history . "\r\n".
            "reason: " . $reason . "\r\n".
            "time: " . $time . "\r\n".
            "levelAmount: " . $levelAmount . "\r\n" .
            "content-type: application/json; charset=UTF-8 \r\n",
        "content" => $nodes
    ]
];

$context = stream_context_create($opts);
$linkName = "http://192.168.0.4:8203/sensor" . $topic;
$response = file_get_contents($linkName, false, $context);
echo $response;
?>
