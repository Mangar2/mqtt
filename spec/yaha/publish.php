<?php

$topic = filter_input(INPUT_POST, "topic");
$value = filter_input(INPUT_POST, "value");
$postdata = file_get_contents("php://input");

if (empty($topic) && !empty($postdata)) {
    $request = json_decode($postdata);
    $topic = filter_var($request->topic);
    $value = filter_var($request->value);
}

$topic = str_replace('%2F', '/', $topic);

// Create a stream
$url = 'http://localhost:8183/publish';
$timestamp = (new DateTime())->format('c');
$json_data = json_encode(
    array(
        'topic' => $topic, 
        'reason' => array(array('message' => 'Request by browser', 'timestamp' => $timestamp)),
        'value' => $value));

$options = array(
    'http' => array(
        'method'  => 'PUT',
        'header'  =>
            "Content-type: application/json\r\n".
            "qos: 1\r\n".
            "Accept: application/json\r\n".
            "Connection: close\r\n".
            "Content-length: " . strlen($json_data) . "\r\n",
        'content' => $json_data
    )
);

$context = stream_context_create($options);
$response = file_get_contents($url, false, $context);
echo json_encode($response);
?>